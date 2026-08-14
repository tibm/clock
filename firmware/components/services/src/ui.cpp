#include "clk/services/ui.hpp"

#include <cinttypes>
#include <cstdlib>

#include "clk/domain/hand.hpp"
#include "clk/log.hpp"

namespace clk::svc {
namespace {

using hal::pixels::Rgbw;

constexpr uint32_t kTickMs = 20;          // §6.6: PCNT is polled and diffed every 20 ms
constexpr uint8_t kPowerEveryTicks = 12;  // ~4 Hz; it is a battery, not a trigger
constexpr uint8_t kTapEveryTicks = 2;     // 10 Hz: a snooze tap must not feel laggy
constexpr uint8_t kLowBattPct = 20;
constexpr uint32_t kTapAckMs = 400;  // the tap flash, long enough to be seen on a 50 Hz feed

// Chain order is dial first (§9.2): 0-1 on-PCB dial wash, 2-6 the status row through J12.
constexpr std::size_t kBell = 2, kAlarmPx = 3, kClockPx = 4, kVol = 5, kBatt = 6;

// The volume gauge (README §12): 0 % straight up, 100 % at 10 o'clock the long way round,
// so the whole range is 300 degrees of dial and both hands carry it together.
constexpr int32_t kVolSweepDeg = 300;
constexpr int32_t kUstepsPerVolPct = domain::kRev * kVolSweepDeg / 360 / 100;
static_assert(kUstepsPerVolPct * 100 == domain::kRev * kVolSweepDeg / 360, "gauge must be exact");

// The preview chime, until `audio` (§6.2) owns the amp: you cannot set a volume you cannot
// hear, so the mode plays at the level it is editing.
constexpr uint32_t kChimeMs = 140;
constexpr uint32_t kChimeEveryMs = 1500;

const char* name_of(Ui::Mode m) noexcept {
    switch (m) {
        case Ui::Mode::Idle:
            return "idle";
        case Ui::Mode::Bell:
            return "bell";
        case Ui::Mode::Alarm:
            return "alarm";
        case Ui::Mode::Clock:
            return "clock";
        case Ui::Mode::Volume:
            return "volume";
        case Ui::Mode::Pairing:
            return "pairing";
    }
    return "?";
}

int wrap_day(int minutes) noexcept {
    minutes %= 1440;
    return minutes < 0 ? minutes + 1440 : minutes;
}

}  // namespace

Ui::Ui() noexcept : ActiveObject({"ui", 10, 4096, kTickMs}) {}

Ui& ui() noexcept {
    static Ui u;
    return u;
}

void Ui::on_start() {
    const auto k = hal::knob::read();
    knob_last_ = k.ok() ? k.v.count : 0;
    // Seed the tap counter here, exactly as the knob is seeded: whatever it already reads is
    // history, not a tap the user just made.
    const auto s = hal::imu::read();
    taps_last_ = s.ok() ? s.v.taps : 0;
    last_input_us_ = port::now_us();
    CLK_LOGI(ui, "up; knob %s", k.ok() ? "present" : clk::name(k.st));
    cue();
    render();
    publish();
}

Ui::Snapshot Ui::snapshot() const noexcept {
    port::Lock lk{mx_};
    return snap_;
}
Ui::Tuning Ui::tuning() const noexcept {
    port::Lock lk{mx_};
    return tune_;
}
void Ui::set_tuning(Tuning const& t) noexcept {
    port::Lock lk{mx_};
    tune_ = t;
}
domain::AnimCfg Ui::anim_cfg() const noexcept {
    port::Lock lk{mx_};
    return anim_;
}
void Ui::set_anim_cfg(domain::AnimCfg const& c) noexcept {
    port::Lock lk{mx_};
    anim_ = c;
}

void Ui::set_mode(Mode m) noexcept { post(ModeSet{static_cast<uint8_t>(m)}); }

void Ui::on_event(Event const& e) {
    if (const auto* m = as<ModeSet>(e)) return enter(static_cast<Mode>(m->mode));
    if (const auto* d = as<KnobDelta>(e)) return rotate(d->counts);
    if (const auto* p = as<KnobPress>(e)) {
        if (!p->down) press(p->held_ms);
        return;
    }
    if (as<Tap>(e)) {
        // Tap-to-snooze (README §12).  Until the alarm exists it is a visible
        // acknowledgement, which is still the interaction worth tuning: a tap must feel
        // like it did something.  It goes on the OVERLAY layer, so it outranks whatever the
        // mode is showing and hands the pixel straight back when it has faded -- the
        // previous arrangement painted over it on the next tick and nobody ever saw it.
        CLK_LOGI(ui, "tap");
        arm(over_, kBell, domain::ramp_down(domain::kWhite, level(), kTapAckMs));
        last_input_us_ = port::now_us();
    }
}

void Ui::on_tick() {
    poll_knob();
    poll_tap();
    watch_battery();

    const auto t = tuning();
    const uint32_t limit = mode_ == Mode::Pairing ? t.pair_timeout_ms : t.timeout_ms;
    if (mode_ != Mode::Idle && port::now_us() - last_input_us_ > limit * 1000ull) {
        CLK_LOGI(ui, "timeout -> idle (settings kept)");
        enter(Mode::Idle);
    }

    chime_tick();
    cue();
    render();
    publish();
}

// Nothing posted Tap.  The event existed, the handler below existed, `sim tap` and the app's
// tap button existed -- and in between there was no code at all, so the one gesture README
// §12 calls tap-to-snooze incremented a counter nobody read.  The BNO085's tap is an
// interrupt on the board and belongs to the sensor driver when that exists (§12.0.2); until
// then this is the same arrangement `ui` already has with the knob and the cell, which is to
// diff a HAL counter on its own tick.  MOVE IT when the driver lands: the handler does not
// change, only who posts to it.  The counter is monotonic, so a missed poll costs no taps.
void Ui::poll_tap() noexcept {
    if (++tap_div_ < kTapEveryTicks) return;
    tap_div_ = 0;
    const auto s = hal::imu::read();
    if (!s.ok()) return;
    if (s.v.taps != taps_last_) {
        taps_last_ = s.v.taps;
        post(Tap{});
    }
}

// The low-cell warning is the one thing on the pixels that no INPUT causes.  Poll it slowly
// -- on the board this is an ADC read, and four times a second is plenty for a battery.
void Ui::watch_battery() noexcept {
    if (++power_div_ < kPowerEveryTicks) return;
    power_div_ = 0;
    const auto p = hal::power::read();
    batt_warn_ = p.ok() && p.v.soc_pct < kLowBattPct && !p.v.plugged;
}

// PCNT is hardware quadrature with a glitch filter; there is no ISR, we diff the count
// (§3.3).  The switch is an IRQ on target -- here the same 20 ms poll sees it.
void Ui::poll_knob() noexcept {
    const auto k = hal::knob::read();
    if (!k.ok()) return;

    if (k.v.count != knob_last_) {
        const int32_t d = k.v.count - knob_last_;
        knob_last_ = k.v.count;
        rotate(d);
    }
    if (k.v.sw != sw_last_) {
        sw_last_ = k.v.sw;
        if (k.v.sw) {
            sw_down_us_ = port::now_us();
        } else {
            press(static_cast<uint32_t>((port::now_us() - sw_down_us_) / 1000ull));
        }
    }
    if (!k.v.sw) return;

    // A finger on the knob is input, so the five-second timeout must not fire underneath a
    // deliberate ten-second hold.
    last_input_us_ = port::now_us();
    const auto t = tuning();
    if (!pair_armed_ && mode_ != Mode::Pairing &&
        port::now_us() - sw_down_us_ >= t.pair_press_ms * 1000ull) {
        // The hold commits HERE, at the ten-second mark, and lights up to say so -- a
        // gesture with no feedback until you let go is a gesture nobody discovers.  The
        // release that follows is spent (see press()).
        pair_armed_ = true;
        CLK_LOGI(ui, "held %" PRIu32 " ms -> BLE pairing", t.pair_press_ms);
        enter(Mode::Pairing);
    }
}

void Ui::enter(Mode m) noexcept {
    // The one refusal in the whole HSM (README §12).  It lives here rather than in press()
    // so that every route in -- the knob, `ui mode clock`, the app -- obeys it.
    if (m == Mode::Clock && net_owns_time()) {
        CLK_LOGW(ui, "clock: the network owns the time -- refused");
        arm(over_, kClockPx, domain::flash(domain::kRed, level(), 3));
        m = Mode::Volume;
    }
    // Leaving `clock` is what writes the time; there is exactly one way out of that mode
    // that does not, and it is the refusal above (which never entered it).
    if (mode_ == Mode::Clock && m != Mode::Clock) commit_clock();
    if (mode_ == Mode::Volume && m != Mode::Volume) chime_stop();

    mode_ = m;
    last_input_us_ = port::now_us();
    // Part of a turn left over from the last mode is not part of this one.
    counts_resid_ = 0;
    arm_resid_ = 0;

    if (m == Mode::Alarm) set_min_of_day_ = alarm_min_of_day_;
    if (m == Mode::Clock && chrono_) {
        const auto c = chrono_->snapshot();
        set_min_of_day_ = c.hour * 60 + c.minute;
    }
    if (m == Mode::Volume) chime_at_us_ = port::now_us();

    // The hands are the readout in every mode but Idle and Pairing, so the clock has to let
    // go of them first: chrono pushes a fresh target every second, and it would take the
    // preview back between one turn of the knob and the next.
    if (chrono_) chrono_->set_follow(m == Mode::Idle || m == Mode::Pairing);
    show_hands();
    CLK_LOGI(ui, "mode %s", name_of(m));
}

// 64 CPR optical, no detent: sensitivity is entirely a firmware mapping, and the curve is
// what lets the same knob nudge one minute and wind twelve hours (§6.6).
int32_t Ui::gain_for(int32_t mag) const noexcept {
    const auto t = tuning();
    const int32_t top = t.accel_factor > 1 ? t.accel_factor : 1;
    if (mag <= t.slow_max) return 1;
    if (t.fast_at <= t.slow_max || mag >= t.fast_at) return top;
    return 1 + (mag - t.slow_max) * (top - 1) / (t.fast_at - t.slow_max);
}

void Ui::rotate(int32_t counts) noexcept {
    if (counts == 0) return;
    last_input_us_ = port::now_us();
    // A turn in Idle wakes nothing -- press first.  A turn while pairing is not a control
    // either; there is nothing on that screen to adjust.
    if (mode_ == Mode::Idle || mode_ == Mode::Pairing) return;

    const auto t = tuning();

    if (mode_ == Mode::Bell) {
        // Direction, not distance: clockwise arms, anticlockwise disarms, and how far you
        // turned makes no difference at all.  The deadband is because an optical encoder
        // with no detent will report a count for a knock on the table.
        arm_resid_ += counts;
        if (std::abs(arm_resid_) < t.arm_deadband) return;
        const bool want = arm_resid_ > 0;
        arm_resid_ = 0;
        if (want == alarm_armed_) return;
        alarm_armed_ = want;
        CLK_LOGI(ui, "alarm %s", want ? "ON" : "OFF");
        show_hands();
        return;
    }

    // Carry the remainder.  A hand on the knob does not deliver its counts in one lump: PCNT
    // is read every tick and the UI streams a dragged knob as one- and two-count deltas, so
    // dividing each delta on its own threw away the whole turn whenever counts_per_minute
    // was more than 1 -- the knob moved nothing at all, at any speed, unless you flicked it.
    const int32_t per = t.counts_per_minute ? t.counts_per_minute : 1;
    counts_resid_ += counts * gain_for(std::abs(counts));
    const int32_t units = counts_resid_ / per;  // truncates toward zero, both signs
    counts_resid_ -= units * per;
    if (units == 0) return;

    switch (mode_) {
        case Mode::Alarm:
            set_min_of_day_ = wrap_day(set_min_of_day_ + units);
            alarm_min_of_day_ = set_min_of_day_;
            break;
        case Mode::Clock:
            set_min_of_day_ = wrap_day(set_min_of_day_ + units);
            break;
        case Mode::Volume: {
            const int v = static_cast<int>(volume_) + units;
            volume_ = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
            hal::audio::set_volume_pct(volume_);
            chime_at_us_ = port::now_us();  // and hear the new level straight away
            break;
        }
        default:
            break;
    }
    show_hands();
}

void Ui::press(uint32_t held_ms) noexcept {
    last_input_us_ = port::now_us();

    // The ten-second hold already acted while the knob was still down.  Letting go of it is
    // not also a long press, and it is certainly not the next mode.
    if (pair_armed_) {
        pair_armed_ = false;
        return;
    }

    if (held_ms >= tuning().long_press_ms) {
        CLK_LOGI(ui, "long press -> idle");
        enter(Mode::Idle);  // commits a clock set on the way out
        return;
    }

    switch (mode_) {
        case Mode::Idle:
            enter(Mode::Bell);
            break;
        case Mode::Bell:
            enter(Mode::Alarm);
            break;
        case Mode::Alarm:
            enter(Mode::Clock);
            break;
        case Mode::Clock:
            enter(Mode::Volume);
            break;
        case Mode::Volume:
        case Mode::Pairing:
            enter(Mode::Idle);
            break;
    }
}

void Ui::commit_clock() noexcept {
    if (!chrono_) return;
    const auto c = chrono_->snapshot();
    const int64_t day = c.epoch_ms - ((c.hour * 60 + c.minute) * 60 + c.second) * 1000ll;
    chrono_->set_time(day + set_min_of_day_ * 60'000ll);
}

// `net` (§6.7) does not exist yet, so the two facts it will report live on `chrono` -- which
// is the right owner anyway, being the time authority.  The rear toggle is hardware and is
// read here directly.
bool Ui::net_owns_time() const noexcept {
    const auto r = hal::expander::get(hal::expander::Sig::RadioOff);
    // The pin idles HIGH through the expander pull-up and the rear toggle pulls it LOW, so
    // a low reading is "radios disabled" and an unreadable expander fails to radios-enabled,
    // exactly as the harness does (README §16d).
    if (r.ok() && !r.v) return false;
    if (!chrono_) return false;
    const auto c = chrono_->snapshot();
    return c.net_provisioned && c.net_synced;
}

// The hands ARE the readout (README §5): the alarm time, the time being set, or the volume.
void Ui::show_hands() noexcept {
    if (!motion_) return;
    int show = -1;
    switch (mode_) {
        case Mode::Bell:
            // Armed, the dial shows when it will go off.  Disarmed, it says so with the
            // plainest thing a pair of hands can say: straight up, both of them.
            show = alarm_armed_ ? alarm_min_of_day_ : 0;
            break;
        case Mode::Alarm:
        case Mode::Clock:
            show = set_min_of_day_;
            break;
        case Mode::Volume: {
            const int32_t u = static_cast<int32_t>(volume_) * kUstepsPerVolPct;
            motion_->goto_usteps(u, u, true);
            return;
        }
        default:
            return;  // Idle and Pairing: chrono has the hands back
    }
    const auto p = domain::for_time(show / 60, show % 60);
    motion_->goto_usteps(p.hour, p.minute, true);
}

// ---- light ---------------------------------------------------------------------------------

uint8_t Ui::level() const noexcept {
    const auto t = tuning();
    return static_cast<uint8_t>(255u * t.brightness / 100u);
}

// Modes 1 and 2 answer the same question -- is the alarm on? -- so they say it the same way.
domain::Anim Ui::alarm_cue() const noexcept {
    const uint8_t l = level();
    return alarm_armed_ ? domain::blink(domain::kRed, l) : domain::breathe(domain::kWhite, l);
}

// Arm an animation, but only if it is actually a DIFFERENT one.  This is the whole trick
// behind five pixels breathing in sync: cue() runs 50 times a second, and re-arming an
// unchanged cue would pin every animation to t=0 forever -- a breath would never get past
// its first millisecond, and a burst would never end.
void Ui::arm(domain::Anim* layer, std::size_t i, domain::Anim a) noexcept {
    if (domain::same(layer[i], a)) return;
    a.t0_us = port::now_us();
    layer[i] = a;
}

void Ui::fade_out(std::size_t i) noexcept {
    auto& a = base_[i];
    if (a.pattern == domain::Pattern::Off) return;
    if (a.pattern == domain::Pattern::RampDown) {
        // Already on its way out.  Re-arming it every tick from its own falling level would
        // make the fade an asymptote that never quite arrives, and "0 light when idle" (R2)
        // is not a limit, it is a value.
        if (domain::done(a, anim_, port::now_us())) a = domain::off();
        return;
    }
    arm(base_, i, domain::ramp_down(a.color, domain::envelope(a, anim_, port::now_us())));
}

// What the MODE wants each pixel to be doing.  This table is the UX (README §12).
void Ui::cue() noexcept {
    domain::Anim want[hal::pixels::kCount]{};  // Off unless something below says otherwise
    const uint8_t l = level();

    switch (mode_) {
        case Mode::Idle:
            break;  // zero emission when idle -- hard invariant (R2/R6)
        case Mode::Bell:
            want[kBell] = alarm_cue();
            break;
        case Mode::Alarm:
            want[kAlarmPx] = alarm_cue();
            break;
        case Mode::Clock:
            want[kClockPx] = domain::ramp_up(domain::kWhite, l);
            break;
        case Mode::Volume:
            want[kVol] = domain::ramp_up(domain::kWhite, l);
            break;
        case Mode::Pairing:
            // All five, armed in the same pass, so they share a t0 and breathe as one.
            for (std::size_t i = kBell; i <= kBatt; ++i)
                want[i] = domain::breathe(domain::kBlue, l);
            break;
    }

    // The cell warning is the one emitter no input causes, and the one documented exception
    // to zero-emission-when-idle: a clock that dies in the night without saying so is worse
    // than an amber pixel.  Pairing owns the whole row, so it wins for those two minutes.
    if (batt_warn_ && mode_ != Mode::Pairing) want[kBatt] = domain::breathe(domain::kAmber, l);

    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        if (want[i].pattern == domain::Pattern::Off) {
            fade_out(i);
        } else {
            arm(base_, i, want[i]);
        }
    }
}

// One pass over both layers, once a tick.  The chain is only written when the frame we
// computed differs from the frame we last wrote -- which keeps the SPI quiet on an idle
// clock AND leaves `ui led` (§9.3) in possession of a pixel nothing is animating.
void Ui::render() noexcept {
    const uint64_t now = port::now_us();
    Rgbw frame[hal::pixels::kCount];
    bool changed = force_write_;

    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        if (over_[i].pattern != domain::Pattern::Off && domain::done(over_[i], anim_, now)) {
            over_[i] = domain::off();  // transient finished; the mode gets its pixel back
        }
        auto const& a = over_[i].pattern != domain::Pattern::Off ? over_[i] : base_[i];
        frame[i] = domain::render(a, anim_, now);
        changed = changed || !(frame[i] == shown_[i]);
    }
    if (!changed) return;

    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        shown_[i] = frame[i];
        hal::pixels::set(i, frame[i]);
    }
    hal::pixels::refresh();
    force_write_ = false;
}

// ---- sound -----------------------------------------------------------------------------------
// A stand-in for `audio` (§6.2), same arrangement as the tap counter above: the gesture is
// real and worth tuning now, the ownership moves when the AO lands.  MOVE IT then -- `ui`
// has no business touching I2S once something else does.
void Ui::chime_tick() noexcept {
    if (mode_ != Mode::Volume) return;
    const uint64_t now = port::now_us();
    if (chime_off_us_ && now >= chime_off_us_) {
        hal::audio::enable(false);
        chime_off_us_ = 0;
    }
    if (!chime_off_us_ && chime_at_us_ && now >= chime_at_us_) {
        hal::audio::enable(true);
        chime_off_us_ = now + kChimeMs * 1000ull;
        chime_at_us_ = now + kChimeEveryMs * 1000ull;
    }
}

void Ui::chime_stop() noexcept {
    if (chime_off_us_) hal::audio::enable(false);
    chime_off_us_ = chime_at_us_ = 0;
}

void Ui::publish() noexcept {
    const auto t = tuning();
    const uint64_t now = port::now_us();
    const uint32_t limit = mode_ == Mode::Pairing ? t.pair_timeout_ms : t.timeout_ms;
    const uint64_t since = now - last_input_us_;
    const auto left = static_cast<uint32_t>(since / 1000ull >= limit ? 0 : limit - since / 1000ull);
    // Read before the lock: net_owns_time() takes chrono's, and two services' mutexes held
    // in one order here and the other order there is the whole recipe for a deadlock.
    const bool locked = net_owns_time();
    port::Lock lk{mx_};
    snap_.mode = mode_;
    snap_.mode_name = name_of(mode_);
    snap_.alarm_armed = alarm_armed_;
    snap_.alarm_hour = alarm_min_of_day_ / 60;
    snap_.alarm_minute = alarm_min_of_day_ % 60;
    snap_.volume = volume_;
    snap_.counts_per_minute = tune_.counts_per_minute;
    snap_.idle_in_ms = mode_ == Mode::Idle ? 0 : left;
    snap_.net_locked = locked;
    snap_.held_ms = sw_last_ ? static_cast<uint32_t>((now - sw_down_us_) / 1000ull) : 0;
}

}  // namespace clk::svc
