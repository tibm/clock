#include "clk/services/ui.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "clk/domain/hand.hpp"
#include "clk/log.hpp"
#include "clk/services/supervisor.hpp"

namespace clk::svc {
namespace {

// NVS key (§7.5).  Fifteen characters is the NVS limit and this is eight.
constexpr const char* kKeyInput = "ui.input";
constexpr const char* kKeyAlarm = "ui.alarm";  // minute of day -- read once, to seed ui.week
constexpr const char* kKeyArmed = "ui.armed";
// The phone's week, "<days hex> <min mon> .. <min sun>" (§6.6f), and the knob's one-off as two
// local minutes/days.  int32 is plenty: local minutes pass 2^31 in the year 6053.
constexpr const char* kKeyWeek = "ui.week";
constexpr const char* kKeyOvrAt = "ui.ovr_at";
constexpr const char* kKeyOvrDay = "ui.ovr_day";
constexpr int32_t kNvsNone = INT32_MIN;

namespace al = domain::alarm;

using hal::pixels::Rgbw;

constexpr uint32_t kTickMs = 20;          // §6.6: PCNT is polled and diffed every 20 ms
constexpr uint8_t kPowerEveryTicks = 12;  // ~4 Hz; it is a battery, not a trigger
constexpr uint8_t kTapEveryTicks = 2;     // 10 Hz: a snooze tap must not feel laggy
constexpr uint8_t kLowBattPct = 20;

// How often gravity is asked which way up the cube is (§6.1d).  The two numbers ARE the
// feature's power budget: plugged in you can turn the cube on the shelf and watch the hands
// come round after it, so the poll has to be fast enough to feel like a response rather than
// a refresh; on the battery nobody is watching a clock they are carrying, and the same answer
// costs four times less.  Sim time, so `sim warp` scales them with everything else.
constexpr uint32_t kLevelPluggedMs = 500;
constexpr uint32_t kLevelBatteryMs = 2000;

// Chain order is dial first (§9.2): 0-1 on-PCB dial wash, 2-6 the status row through J12.
constexpr std::size_t kDial0 = 0, kDial1 = 1;
constexpr std::size_t kBell = 2, kAlarmPx = 3, kClockPx = 4, kVol = 5, kBatt = 6;

// A fault code (§6.6g): the pixel that names the fault blinks red, hard-edged -- `Blink` is the
// pattern no mode uses, kept for this -- and SLOW, so it reads as "something is wrong" rather
// than as the ringing alarm's urgent 220 ms blink on the bell.
constexpr uint32_t kFaultBlinkMs = 2000;

// How many breaths the bell takes while the dial wash is up.  A COUNT rather than a duration:
// the bell has to end dark exactly on the boundary, and a period that does not divide the
// window leaves it cut off half way up -- which reads as a glitch, not as an ending.
constexpr uint8_t kTapBreaths = 2;

// The volume gauge (README §12): 0 % straight up, 100 % at 10 o'clock the long way round,
// so the whole range is 300 degrees of dial and both hands carry it together.  The remaining
// 60 degrees -- between the 10 and the 12 -- is off the scale, and the hands never go there:
// every move inside the mode carries the direction the level is changing, so the gauge is
// swept rather than short-cut across its own dead zone.
constexpr int32_t kVolSweepDeg = 300;
constexpr int32_t kUstepsPerVolPct = domain::kRev * kVolSweepDeg / 360 / 100;
static_assert(kUstepsPerVolPct * 100 == domain::kRev * kVolSweepDeg / 360, "gauge must be exact");

// Both hands stacked on the 6.  Deliberately not a time: at 6:30 the hour hand is halfway to
// the 7, so a pair of hands agreeing on the 6 is a reading no working clock ever shows -- and
// that is the point, it is what "the alarm is off" looks like (§6.6b).
constexpr int32_t kSouth = domain::kRev / 2;

// Setting a time is PACED to what the movement can draw (§6.6d, drain_setting below).  The
// pace itself is derived from `motion`'s v_max; these are the bounds on it.
constexpr uint32_t kPaceFloorMs = 20;  // never faster than `ui`'s own tick
constexpr uint32_t kPaceCeilMs = 500;  // ... and never so slow the knob feels dead
// The encoder has no detent -- it is optical, 256 counts/rev -- but four counts is the notch a
// detented knob would have, and it is the unit `ui knob counts` is quoted in.  drain_setting()
// will not split one.
constexpr int32_t kDetentCounts = 4;

// The preview chime, until `audio` (§6.2) owns the amp: you cannot set a volume you cannot
// hear, so the mode plays at the level it is editing.  It became an actual note on
// 2026-09-13 -- before that it toggled `SPK_SD` around silence, which on the bench was a tick
// and nothing else.  A: 440 Hz, because a preview wants to be recognisable rather than
// attention-getting; the alarm's own tones arrive as assets with `storage`.
constexpr uint32_t kChimeHz = 440;
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
        case Ui::Mode::Ringing:
            return "ringing";
        case Ui::Mode::Snoozed:
            return "snoozed";
    }
    return "?";
}

int wrap_day(int minutes) noexcept {
    minutes %= 1440;
    return minutes < 0 ? minutes + 1440 : minutes;
}

void week_to_str(al::Week const& w, char* out, std::size_t cap) noexcept {
    std::snprintf(out, cap, "%02x %d %d %d %d %d %d %d", w.days, w.min[0], w.min[1], w.min[2],
                  w.min[3], w.min[4], w.min[5], w.min[6]);
}

bool week_from_str(const char* s, al::Week& w) noexcept {
    unsigned days = 0;
    int m[al::kDays] = {};
    if (std::sscanf(s, "%x %d %d %d %d %d %d %d", &days, &m[0], &m[1], &m[2], &m[3], &m[4], &m[5],
                    &m[6]) != 8)
        return false;
    al::Week t;
    t.days = static_cast<uint8_t>(days);
    for (int d = 0; d < al::kDays; ++d) t.min[d] = static_cast<int16_t>(m[d]);
    if (days > al::kAllDays || !t.valid()) return false;
    w = t;
    return true;
}

int64_t ovr_from_nvs(const char* key) noexcept {
    const auto v = hal::store::get_i32(key);
    return v.ok() && v.v != kNvsNone ? v.v : al::kNone;
}

}  // namespace

Ui::Ui() noexcept : ActiveObject({"ui", 10, 4096, kTickMs}) {}

Ui& ui() noexcept {
    static Ui u;
    return u;
}

void Ui::on_start() {
    // The bench gate first: it decides whether the seeding read below means anything.
    if (const auto v = hal::store::get_i32(kKeyInput); v.ok()) input_ = v.v != 0;
    if (const auto v = hal::store::get_i32(kKeyArmed); v.ok()) alarm_armed_ = v.v != 0;
    char wk[64] = {};
    if (hal::store::get_str(kKeyWeek, wk, sizeof wk) != Status::Ok || !week_from_str(wk, week_)) {
        // No week yet: the single daily alarm this clock had before 2026-09-30 becomes a week
        // of seven identical days, which rings exactly as it did.
        const auto v = hal::store::get_i32(kKeyAlarm);
        week_ = al::Week::daily(
            static_cast<int16_t>(v.ok() && v.v >= 0 && v.v < 24 * 60 ? v.v : al::kDefaultMin));
    }
    ovr_ = {ovr_from_nvs(kKeyOvrAt), ovr_from_nvs(kKeyOvrDay)};

    const auto k = hal::knob::read();
    knob_last_ = k.ok() ? k.v.count : 0;
    // Seed the tap counter here, exactly as the knob is seeded: whatever it already reads is
    // history, not a tap the user just made.
    const auto s = hal::imu::read();
    taps_last_ = s.ok() ? s.v.taps : 0;
    last_input_us_ = port::now_us();
    CLK_LOGI(ui, "up; knob %s%s", k.ok() ? "present" : clk::name(k.st),
             input_ ? "" : " -- INPUT OFF, `ui input on` to restore");
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

domain::AmbientCfg Ui::ambient_cfg() const noexcept {
    port::Lock lk{mx_};
    return amb_;
}
void Ui::set_ambient_cfg(domain::AmbientCfg const& c) noexcept {
    port::Lock lk{mx_};
    amb_ = c;
}

void Ui::set_mode(Mode m) noexcept { post(ModeSet{static_cast<uint8_t>(m)}); }

void Ui::on_event(Event const& e) {
    if (const auto* m = as<ModeSet>(e)) {
        const auto want = static_cast<Mode>(m->mode);
        // Snoozing is an answer to a ring; there is nothing to snooze otherwise.
        if (want == Mode::Snoozed && mode_ != Mode::Ringing) return;
        if (want == Mode::Ringing) {
            CLK_LOGI(ui, "ALARM fires (asked for)");
            if (const auto n = next_alarm(); n.src != al::Src::None)
                alarm_rung_min_ = al::min_of_day(n.at);
        }
        return enter(want);
    }
    if (const auto* a = as<AlarmCfg>(e)) {
        const bool m_ok = a->min_of_day >= 0 && a->min_of_day < 24 * 60;
        switch (a->op) {
            case AlarmCfg::Daily:
                if (m_ok) {
                    week_ = al::Week::daily(a->min_of_day);
                    ovr_ = {};
                    save_override();
                }
                break;
            case AlarmCfg::Week: {
                port::Lock lk{mx_};
                if (week_in_.valid()) week_ = week_in_;
                break;
            }
            case AlarmCfg::Next:
                if (m_ok) make_override(a->min_of_day);
                break;
            case AlarmCfg::NextClear:
                ovr_ = {};
                save_override();
                break;
        }
        if (a->armed >= 0) alarm_armed_ = a->armed != 0;
        save_alarm();
        const auto n = next_alarm();
        CLK_LOGI(ui, "alarm %s; next %s", alarm_armed_ ? "armed" : "off",
                 n.src == al::Src::None       ? "none"
                 : n.src == al::Src::Override ? "one-off"
                                              : "scheduled");
        // The dial follows a change made while somebody is looking at it -- unless the knob is
        // mid-edit, where the edit is what they are looking at.
        if (mode_ == Mode::Bell) show_hands();
        return;
    }
    if (const auto* d = as<KnobDelta>(e)) return rotate(d->counts);
    if (const auto* p = as<KnobPress>(e)) {
        if (!p->down) press(p->held_ms);
        return;
    }
    if (as<Tap>(e)) {
        // Tap-to-snooze (README §12).  Otherwise it is a visible acknowledgement: a tap must
        // feel like it did something.
        CLK_LOGI(ui, "tap");
        last_input_us_ = port::now_us();
        if (mode_ == Mode::Ringing) return enter(Mode::Snoozed);
        tap_ack();
    }
}

void Ui::on_tick() {
    poll_knob();
    poll_tap();
    poll_level();
    watch_battery();
    watch_faults();
    watch_room();
    drain_setting();  // release banked counts at the speed the hands can render them
    watch_alarm();

    // ONE timeout, and every mode obeys it -- except a pairing window the radio is actually
    // holding open.  Five seconds is not long enough to get a phone out of a pocket, and the
    // window has an end of its own (Net::kDefaultWindowMs) that closes this mode through
    // watch_pairing().  Without a radio, pairing is five seconds of blue like any other mode.
    watch_pairing();
    const auto t = tuning();
    const bool radio_holds = mode_ == Mode::Pairing && net_ && net_->snapshot().pairing;
    // Nor a ringing alarm: it ends by an answer, or by Tuning::ring_max_min (watch_alarm).
    if (mode_ != Mode::Idle && !radio_holds && !ringing() &&
        port::now_us() - last_input_us_ > t.timeout_ms * 1000ull) {
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
    imu_ok_ = s.ok();
    if (!s.ok()) return;
    imu_ = s.v;
    if (s.v.taps != taps_last_) {
        taps_last_ = s.v.taps;
        post(Tap{});
    }
}

// Which way up the cube is sitting (§6.1d).  Homing finds where the HANDS are; this finds
// where the DIAL is, and they are different questions -- the movement can know its index
// perfectly while the whole clock lies on its side.
//
// Same borrowed arrangement as the tap above, and it moves to `board` with it (§12.0.2): the
// BNO085 is an I2C device on somebody else's bus and this poll is a stand-in for a gravity
// report the sensor hub can be told to send by itself.  What is NOT a stand-in is everything
// after `level_.update()` -- the dead zone, the hysteresis and the confirmation count are the
// feature, and they are in domain/level.hpp where a test can turn a cube over ten thousand
// times a second.
//
// The tick is re-posted on every poll rather than only when it changes.  `motion` drops a
// repeat before it does any work, and this way there is exactly one rule -- the dial ends up
// wherever gravity last said -- instead of a second one about who re-synchronises what after
// `motion tune level` has been off.
void Ui::poll_level() noexcept {
    const uint64_t now = port::now_us();
    const uint32_t every = plugged_ ? kLevelPluggedMs : kLevelBatteryMs;
    // `level_polled_` rather than a zero timestamp: on the host the first tick really can
    // land on time zero, and testing the clock against 0 polled that tick twice.
    if (level_polled_ && now - level_at_us_ < every * 1000ull) return;
    level_polled_ = true;
    level_at_us_ = now;
    const auto s = hal::imu::read();
    // No sensor, no opinion.  The dial keeps whatever it has, which for a clock that has
    // never had an IMU fitted is the printed 12 -- exactly as it was before this existed.
    if (!s.ok()) return;
    if (level_.update(s.v.gx, s.v.gy, s.v.gz)) {
        const auto lv = level_.level();
        CLK_LOGI(ui, "level: up is %.0f deg round the dial, tilt %.2f%s -> tick %d",
                 static_cast<double>(lv.up), static_cast<double>(lv.tilt),
                 lv.flat ? " (flat -- the printed 12 it is)" : "", lv.tick);
    }
    if (motion_) motion_->set_dial_tick(level_.tick());
}

// The low-cell warning is the one thing on the pixels that no INPUT causes.  Poll it slowly
// -- on the board this is an ADC read, and four times a second is plenty for a battery.
void Ui::watch_battery() noexcept {
    if (++power_div_ < kPowerEveryTicks) return;
    power_div_ = 0;
    const auto p = hal::power::read();
    batt_warn_ = p.ok() && p.v.soc_pct < kLowBattPct && !p.v.plugged;
    power_ok_ = p.ok();
    if (p.ok()) power_ = p.v;
    if (p.ok()) plugged_ = p.v.plugged;  // ... and that paces the gravity poll above
    // §6.8 interlock 4.  Unplugging already kills the 12 V rail in hardware; this puts the
    // PWM and BOOST12_EN back to 0 so the reported duty is the truth and a re-plug does not
    // relight the strips on its own.
    if (p.ok() && !p.v.plugged && (hal::wake::warm() || hal::wake::cool()))
        (void)hal::wake::set(0, 0);
}

// The room's light (§6.6g).  The TSL2591 is read on `net`'s thread today (every 5 s, with the
// rest of the status record) and moves to `board` with the other I2C sensors (§6.5) -- reading
// it here as well would be a second caller in a driver written for one, and an auto-range on
// `ui`'s thread is a second of dead knob.  So `ui` takes the last reading; the slew runs every
// tick so the glide is smooth.
void Ui::watch_room() noexcept {
    const uint64_t now = port::now_us();
    if (net_ && power_div_ == 0) {  // ~4 Hz is plenty for a number that changes every 5 s
        const auto st = net_->status();
        // One failed read (an auto-range mid-step) is not the room going away: keep the last
        // good one, and let its AGE decide when it stops counting (AmbientCfg::stale_ms).
        if ((st.flags & transport::kAlsOk) && st.als_age_s != transport::kAgeNever) {
            lux_.ok = true;
            lux_.lux = st.lux;
            lux_.saturated = (st.flags & transport::kAlsSaturated) != 0;
            lux_at_us_ = now - st.als_age_s * 1'000'000ull;
        }
    }
    if (lux_.ok) lux_.age_ms = static_cast<uint32_t>((now - lux_at_us_) / 1000ull);
    room_scale_ = dimmer_.update(lux_, now, ambient_cfg());
    if (dimmer_.target() != room_logged_) {
        room_logged_ = dimmer_.target();
        CLK_LOGI(ui, "room %.2f lux -> light %u%%", static_cast<double>(dimmer_.anchor_lux()),
                 room_logged_ * 100u / 255u);
    }
}

// The supervisor owns the latch; `ui` only shows it.  Polled with the battery: a fault is
// confirmed over three seconds anyway, a quarter of one more is nothing.
void Ui::watch_faults() noexcept {
    if (!sup_ || power_div_ != 0) return;
    faults_ = sup_->snapshot().faults_shown;
}

void Ui::set_input(bool on) noexcept {
    // Re-seed BEFORE opening the gate, never after.  The order is the whole of the
    // correctness here: counts accumulate in PCNT while nobody is reading, so a poll that
    // slipped in between `input_ = true` and the re-seed would diff against a `knob_last_`
    // from minutes ago and deliver the entire interval as one enormous turn.
    if (on) {
        const auto k = hal::knob::read();
        knob_last_ = k.ok() ? k.v.count : knob_last_;
        last_input_us_ = port::now_us();
    }
    {
        port::Lock lk{mx_};
        input_ = on;
    }
    (void)hal::store::set_i32(kKeyInput, on ? 1 : 0);
    CLK_LOGI(ui, "input %s", on ? "on" : "OFF -- the knob drives nothing");
}

bool Ui::input() const noexcept {
    port::Lock lk{mx_};
    return input_;
}

// PCNT is hardware quadrature with a glitch filter; there is no ISR, we diff the count
// (§3.3).  The switch is an IRQ on target -- here the same 20 ms poll sees it.
void Ui::poll_knob() noexcept {
    // Not even a read while input is off.  Skipping the ACTIONS would still consume the HAL's
    // shared delta every 20 ms, which is precisely the thing that makes `sensor knob` hard to
    // read on a bench -- so the isolation has to be at the read, not at the handler.
    if (!input_) return;
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
    // Not out of an alarm: a hand held on the knob of a ringing clock is somebody trying to
    // make it stop, and the long-press dismissal has already done that at 800 ms.
    if (!pair_armed_ && mode_ != Mode::Pairing && !ringing() &&
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
    // Pairing is the radio's window, and the mode is how it looks.  Opening it can fail --
    // the rear toggle has the radio off -- and that is the same refusal as `clock`'s: three
    // red flashes, on the row pairing would have lit, and straight back to idle.
    if (net_ && m == Mode::Pairing && mode_ != Mode::Pairing) {
        if (!net_->pair(true)) {
            CLK_LOGW(ui, "pairing: the radio is off -- refused");
            for (std::size_t i = kBell; i <= kBatt; ++i)
                arm(over_, i, domain::flash(domain::kRed, level(), 3));
            m = Mode::Idle;
        } else {
            net_windows_ = net_->snapshot().windows;
        }
    }
    if (net_ && mode_ == Mode::Pairing && m != Mode::Pairing) (void)net_->pair(false);
    // Leaving `clock` is what writes the time; there is exactly one way out of that mode
    // that does not, and it is the refusal above (which never entered it).
    if (mode_ == Mode::Clock && m != Mode::Clock) commit_clock();
    if (mode_ == Mode::Volume && m != Mode::Volume) chime_stop();
    // Once per edit, not per minute -- and only an edit that changed something.  Opening
    // `alarm` to look at the time and pressing on is not asking for tomorrow to be different.
    if (mode_ == Mode::Alarm && m != Mode::Alarm && set_min_of_day_ != alarm_edit_from_)
        make_override(set_min_of_day_);
    // The sound.  Ringing is the only mode that makes one; leaving it for anything, snooze
    // included, stops it (with a fade -- hal::audio::stop() always releases through the tail).
    if (m == Mode::Ringing && mode_ != Mode::Ringing) {
        ring_since_us_ = port::now_us();
        if (storage_) {
            (void)storage_->ring_alarm();
            const uint8_t a[2] = {static_cast<uint8_t>(alarm_rung_min_ / 60),
                                  static_cast<uint8_t>(alarm_rung_min_ % 60)};
            storage_->log_event(transport::hist::Ev::AlarmFire, a, sizeof a);
        }
    }
    if (mode_ == Mode::Ringing && m != Mode::Ringing && storage_) (void)storage_->stop();
    if (m == Mode::Snoozed) {
        snooze_until_us_ = port::now_us() + tuning().snooze_min * 60'000'000ull;
        CLK_LOGI(ui, "snoozed %u min", static_cast<unsigned>(tuning().snooze_min));
        if (storage_) {
            const uint8_t a[1] = {static_cast<uint8_t>(tuning().snooze_min)};
            storage_->log_event(transport::hist::Ev::AlarmSnooze, a, sizeof a);
        }
    }
    if (ringing() && m == Mode::Idle) {
        CLK_LOGI(ui, "alarm dismissed");
        if (storage_) storage_->log_event(transport::hist::Ev::AlarmDismiss);
    }

    const Mode was = mode_;
    mode_ = m;
    last_input_us_ = port::now_us();
    // Part of a turn left over from the last mode is not part of this one.
    counts_resid_ = 0;
    arm_resid_ = 0;
    last_unit_us_ = 0;  // ... and the first turn in a mode lands straight away

    if (m == Mode::Alarm && was != Mode::Alarm) {
        // The knob edits the NEXT alarm, so it opens on it (§6.6f).
        const auto n = next_alarm();
        int64_t now = 0;
        bool dated = false;
        (void)now_local(now, dated);
        // Nothing coming (every day off): today's time, off or not, is the best place to start.
        set_min_of_day_ = n.src != al::Src::None
                              ? al::min_of_day(n.at)
                              : week_.min[dated ? al::weekday(al::day_of(now)) : 0];
        alarm_edit_from_ = set_min_of_day_;
    }
    if (m == Mode::Clock) {
        // Start from the time the clock is keeping -- and from 12:00 when nothing has ever
        // told it one.  chrono's hour and minute are an offset from an epoch it never had, so
        // an unset clock reads as minutes-since-boot: 00:04 is not a time, it is an uptime,
        // and the hands would open the mode pointing at it.
        set_min_of_day_ = 0;
        if (chrono_) {
            const auto c = chrono_->snapshot();
            if (c.valid) set_min_of_day_ = c.hour * 60 + c.minute;
        }
    }
    if (m == Mode::Volume) chime_at_us_ = port::now_us();

    // The hands are the readout in every mode but Idle and Pairing, so the clock has to let
    // go of them first: chrono pushes a fresh target every second, and it would take the
    // preview back between one turn of the knob and the next.
    if (chrono_)
        chrono_->set_follow(m == Mode::Idle || m == Mode::Pairing || m == Mode::Ringing ||
                            m == Mode::Snoozed);
    show_hands();
    CLK_LOGI(ui, "mode %s", name_of(m));
}

// 64 CPR optical, no detent: sensitivity is entirely a firmware mapping.  The curve belongs
// to the VOLUME now -- a gauge 300 degrees end to end can be crossed in one spin without any
// hand being asked to be in two places.  Setting a time does not use it (§6.6d).
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
    // either; there is nothing on that screen to adjust.  Nor while the alarm rings: turning
    // a knob in the dark is fumbling for it, and fumbling must not change the alarm.
    if (mode_ == Mode::Idle || mode_ == Mode::Pairing || ringing()) return;

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
        save_alarm();
        CLK_LOGI(ui, "alarm %s", want ? "ON" : "OFF");
        show_hands();
        return;
    }

    // Carry the remainder.  A hand on the knob does not deliver its counts in one lump: PCNT
    // is read every tick and the UI streams a dragged knob as one- and two-count deltas, so
    // dividing each delta on its own threw away the whole turn whenever counts_per_minute
    // was more than 1 -- the knob moved nothing at all, at any speed, unless you flicked it.
    const int32_t per = t.counts_per_minute ? t.counts_per_minute : 1;

    if (mode_ == Mode::Volume) {
        // The gauge keeps the acceleration curve: it is 300 degrees end to end and cannot
        // wrap, so 0 to 100 % in one spin is a feature there rather than a hand asked to be
        // in two places (§6.6d).
        counts_resid_ += counts * gain_for(std::abs(counts));
        const int32_t units = counts_resid_ / per;  // truncates toward zero, both signs
        counts_resid_ -= units * per;
        if (units == 0) return;
        const int v = static_cast<int>(volume_) + units;
        volume_ = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
        // The GAUGE is 0..100 and stays 0..100 -- §6.6d's 300 degrees of dial is the product's
        // volume scale and is not a hardware fact.  The AMP's bring-up ceiling is
        // (R-AUDIO-1: PVDD is the 5 V rail until a 15 V brick is in, and a full-scale tone
        // sits on the `-GB` protector's trip), so what is ASKED FOR is clamped and what is
        // SHOWN is not.  ⚠ Until then the preview chime stops getting louder above
        // kMaxVolPct while the hands keep climbing; drop this line when FIRMWARE.md §12.2's two
        // hardware gates close and hal::audio stops refusing.
        const uint8_t ask = volume_ < hal::audio::kMaxVolPct ? volume_ : hal::audio::kMaxVolPct;
        (void)hal::audio::set_volume_pct(ask);
        chime_at_us_ = port::now_us();  // and hear the new level straight away
        show_hands(units > 0 ? 1 : -1);
        return;
    }

    // Alarm and Clock: hold the counts and let the tick spend them one minute at a time, no
    // faster than the hands can render them (see drain_setting).  No acceleration curve here
    // -- §6.6d's twelvefold gain made a single 20 ms poll worth two hours of dial, which the
    // minute hand cannot be asked to draw.
    counts_resid_ += counts;
    drain_setting();  // ... and let the first minute land now rather than on the next tick
}

// One minute of dial at the speed the movement is actually set to.  Exactly that, now that
// the surplus is dropped rather than banked: every millisecond of margin here is a minute of
// somebody's turn thrown away, and a hand that has to accelerate into the first minute is
// one or two behind for the rest of the wind -- a tenth of a second of tail, once.
uint32_t Ui::pace_ms() const noexcept {
    int32_t v = motion_ ? motion_->tuning().v_max : 6000;
    if (v < 1) v = 1;
    const auto ms = static_cast<uint32_t>(1000ll * domain::kRev / 60 / v);
    return ms < kPaceFloorMs ? kPaceFloorMs : (ms > kPaceCeilMs ? kPaceCeilMs : ms);
}

// The dial is the readout, so the SETTING may not move faster than the hands can show it --
// and what it cannot show, it does not keep.
//
// v_max is 6000 usteps/s and a minute of dial is 288 of them, so the movement can draw about
// twenty minutes of dial a second.  Beyond that the number is racing a hand that is nowhere
// near it, and the result is not slightly wrong but meaningless: once the setting is more
// than half a turn ahead of the minute hand there is no answer to "which way round", the
// target wraps, a hand in flight gets re-aimed at somewhere it has already passed -- so it
// stops and backs up -- and one steady turn produces a minute hand that stutters, reverses,
// or (at exactly an hour a poll) does not move at all while the hour hand sails on. That last
// one is what "the minute hand follows the hour hand" looks like from the outside (§16c).
//
// So: one minute per release, no faster than the hands run, and counts that arrive faster
// than that are DROPPED (changed 2026-08-17; they used to bank up to two seconds of winding
// and pay it out afterwards).  Banking made a fast spin worth every minute you spun it, and
// the price was a dial that carried on winding for a second or two after the knob stopped --
// a hundred and eighty degrees of minute hand, arriving somewhere you did not choose.  You
// cannot both spin faster than the hands can draw AND stop when they do; of the two, the one
// worth keeping is that what the dial says is what you set.
//
// The sub-minute remainder is still carried, so a deliberate turn loses nothing: a dragged
// knob arrives as a stream of one- and two-count deltas, and dividing each on its own would
// throw away the whole turn.
void Ui::drain_setting() noexcept {
    if (mode_ != Mode::Alarm && mode_ != Mode::Clock) return;
    const int32_t per = tuning().counts_per_minute ? tuning().counts_per_minute : 1;
    const uint64_t now = port::now_us();
    const uint64_t pace_us = pace_ms() * 1000ull;
    const int32_t held = counts_resid_ / per;  // whole minutes, signed
    if (held == 0) {
        // Nothing waiting.  Hold exactly ONE minute of credit: the first detent of a turn
        // lands the moment it arrives, which is what makes the knob feel connected -- and no
        // more than one, or an idle minute would buy a jump as soon as it is touched again.
        // The bank empties every call now, so this cap is what keeps the pace a pace: a
        // blanket reset here would hand out a free minute on every tick of a live turn.
        if (now - last_unit_us_ > pace_us) last_unit_us_ = now - pace_us;
        return;
    }
    // How many minutes the hands have had TIME to draw since the last release.  Elapsed
    // rather than one-per-call, so the rate is the rate whatever the tick is doing -- under
    // `sim warp` a single 20 ms poll is several hundred milliseconds of dial.
    const auto allow = static_cast<int32_t>((now - last_unit_us_) / pace_us);

    const int dir = held > 0 ? 1 : -1;
    const int32_t mag = held > 0 ? held : -held;
    const int32_t units = mag < allow ? mag : allow;
    // Spend what the hands can draw, and drop the rest -- except for ONE DETENT'S WORTH.
    //
    // A detent is the smallest thing a person does to this knob, and it is worth whatever the
    // sensitivity says it is worth: at the shipping four counts a minute that is one minute
    // (so this keeps nothing the strict rule would not), and at `ui knob counts 1` it is four.
    // Splitting one would make the sensitivity setting a lie -- turn the knob one notch, get a
    // quarter of what it promised -- and the whole of it is a fifth of a second of hand.  A
    // QUEUE of detents still cannot accumulate: this is a cap, not a bank.
    const int32_t detent = kDetentCounts / per > 1 ? kDetentCounts / per : 1;
    const int32_t left = mag - units;
    const int32_t keep = left < detent ? left : detent;
    counts_resid_ -= dir * (mag - keep) * per;
    if (units == 0) return;
    last_unit_us_ += static_cast<uint64_t>(units) * pace_us;  // += keeps the average exact
    // ... but credit never accumulates while the knob is still: a minute of not turning must
    // not buy a free minute the instant it is touched again.
    if (now - last_unit_us_ > pace_us) last_unit_us_ = now - pace_us;
    set_min_of_day_ = wrap_day(set_min_of_day_ + dir * units);
    // The hands are still moving to what the knob asked for, so the mode is not idle -- a
    // five-second timeout that fired while the dial was visibly winding would be measured
    // from the wrong thing.
    last_input_us_ = now;
    // The hands follow the KNOB, not the shorter arc: one minute forward is one minute
    // forward even at the half hour, where the shorter arc is fifty-nine minutes back (§6.6e).
    show_hands(dir);
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
        // In idle a long press has nothing to commit, so it is the acknowledgement for a fault
        // code (§6.6g): "I have seen it" -- the row goes dark until something new goes wrong.
        if (mode_ == Mode::Idle && sup_ && faults_) {
            const uint8_t was = sup_->ack();
            faults_ = 0;
            CLK_LOGI(ui, "long press -> fault code acknowledged (0x%02x)", was);
            return;
        }
        CLK_LOGI(ui, "long press -> idle");
        enter(Mode::Idle);  // commits a clock set on the way out; dismisses an alarm
        return;
    }

    switch (mode_) {
        case Mode::Ringing:
            enter(Mode::Snoozed);
            break;
        case Mode::Snoozed:
            // Already snoozed.  A short press is somebody checking; it changes nothing, and
            // the bell still says so.  Only a long press ends it.
            break;
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
    // LOCAL, keeping the date: chrono does the zone arithmetic, DST day included.
    chrono_->set_local_time(set_min_of_day_ / 60, set_min_of_day_ % 60, 0);
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
//
// `dir` is which way the knob just turned, and it is passed straight through to motion --
// see Motion::resolve.  Zero means "this is a jump between two readouts, take the short way":
// entering a mode, or arming the alarm, where there is no turn to follow.
void Ui::show_hands(int dir) noexcept {
    if (!motion_) return;
    int show = -1;
    switch (mode_) {
        case Mode::Bell:
            // Armed, the dial shows when it will go off.  Disarmed, both hands go to the 6 --
            // stacked, which is a reading no working clock can produce, so the dial is
            // visibly saying something rather than displaying a plausible wrong time.
            //
            // "When it will go off" is the NEXT alarm: tomorrow's one-off, or the week's next
            // day.  Armed with nothing coming (every day off) reads as off, because it is.
            {
                const auto n = next_alarm();
                if (!alarm_armed_ || n.src == al::Src::None) {
                    motion_->goto_usteps(kSouth, kSouth, true, dir);
                    return;
                }
                show = al::min_of_day(n.at);
            }
            break;
        case Mode::Alarm:
        case Mode::Clock:
            show = set_min_of_day_;
            break;
        case Mode::Volume: {
            const int32_t u = static_cast<int32_t>(volume_) * kUstepsPerVolPct;
            motion_->goto_usteps(u, u, true, dir);
            return;
        }
        default:
            return;  // Idle and Pairing: chrono has the hands back
    }
    const auto p = domain::for_time(show / 60, show % 60);
    motion_->goto_usteps(p.hour, p.minute, true, dir);
}

// ---- light ---------------------------------------------------------------------------------

uint8_t Ui::level() const noexcept {
    const auto t = tuning();
    return static_cast<uint8_t>(255u * t.brightness / 100u);
}

// Modes 1 and 2 answer the same question -- is the alarm on? -- so they say it the same way.
//
// Both states BREATHE, and the answer is the colour: red for armed, white for off.  A fast
// blink reads as an alarm going off rather than an alarm that is set, and this is a bedroom
// -- the light on the thing you look at last is not the place for something urgent.  Same
// curve, same period, one difference, which is also what makes the pair comparable at a
// glance (§6.6b, changed 2026-08-15).
domain::Anim Ui::alarm_cue() const noexcept {
    const uint8_t l = level();
    return domain::breathe(alarm_armed_ ? domain::kRed : domain::kWhite, l);
}

// Arm an animation, but only if it is actually a DIFFERENT one.  This is the whole trick
// behind five pixels breathing in sync: cue() runs 50 times a second, and re-arming an
// unchanged cue would pin every animation to t=0 forever -- a breath would never get past
// its first millisecond, and a burst would never end.
// `restart` is for the gestures rather than the states: a second tap while the first one is
// still on screen asks for the same animation, so the same-cue check would leave the first one
// running and the tap would look ignored.  A MODE must never pass it -- that is the bug the
// check exists to prevent.
void Ui::arm(domain::Anim* layer, std::size_t i, domain::Anim a, bool restart) noexcept {
    if (!restart && domain::same(layer[i], a)) return;
    a.t0_us = port::now_us();
    layer[i] = a;
}

// What a tap looks like (§6.6b).  The dial washes up over a second, holds five, and takes four
// to leave -- slower out than in, so the room never sees it switch off -- and the bell breathes
// alongside it for the first five seconds.
//
// The COLOUR is the answer to the only question worth asking a clock in the dark: red if the
// alarm is armed, white if it is not.  It is the same red/white rule mode 1 uses (alarm_cue),
// which is what makes the two readable as one instrument rather than two conventions.
//
// All three pixels are armed in one pass, so they share a t0 and the two dial pixels are one
// wash rather than two lights that nearly agree.
void Ui::tap_ack() noexcept {
    const Rgbw c = alarm_armed_ ? domain::kRed : domain::kWhite;
    const uint8_t l = level();
    arm(over_, kDial0, domain::swell(c, l), true);
    arm(over_, kDial1, domain::swell(c, l), true);
    // The bell's window IS the wash's hold: the same five seconds, so retuning one retunes
    // both and the two can never drift into looking like separate events.
    const uint32_t per = anim_.swell_hold_ms / kTapBreaths;
    arm(over_, kBell, domain::breathe(c, l, per, kTapBreaths), true);
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
        case Mode::Ringing:
            // §6.6: "bell pixel red".  Blinking -- this is the one moment the light on the
            // bedside is SUPPOSED to be urgent (compare the armed bell, which breathes).
            want[kBell] = domain::blink(domain::kRed, l);
            break;
        case Mode::Snoozed:
            want[kBell] = domain::breathe(domain::kAmber, l);
            break;
    }

    // The cell warning is the one emitter no input causes, and the one documented exception
    // to zero-emission-when-idle: a clock that dies in the night without saying so is worse
    // than an amber pixel.  Pairing owns the whole row, so it wins for those two minutes.
    if (batt_warn_ && mode_ != Mode::Pairing) want[kBatt] = domain::breathe(domain::kAmber, l);

    // Fault codes (§6.6g) -- the second exception to zero emission, for the same reason: a
    // clock whose hands have stopped is lying, and it should say so.  On the pixel that names
    // what is wrong, unless a mode is using that pixel right now (it is being looked at) or
    // pairing has the whole row.  Over the cell warning: a charger fault is the bigger news.
    if (faults_ && mode_ != Mode::Pairing) {
        struct Code {
            uint8_t bit;
            std::size_t px;
        };
        constexpr Code kCodes[] = {{Supervisor::kFaultHands, kClockPx},
                                   {Supervisor::kFaultCharger, kBatt},
                                   {Supervisor::kFaultAmp, kVol}};
        for (auto const& c : kCodes) {
            const bool mode_owns = (c.px == kClockPx && mode_ == Mode::Clock) ||
                                   (c.px == kVol && mode_ == Mode::Volume);
            if ((faults_ & c.bit) && !mode_owns)
                want[c.px] = domain::blink(domain::kRed, l, kFaultBlinkMs);
        }
    }

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
        // The room scales the level here, at the very end, and never in the cue: changing a
        // cue's level re-arms it, and a breath restarted by a lamp switching off is a glitch.
        auto a = over_[i].pattern != domain::Pattern::Off ? over_[i] : base_[i];
        a.level = domain::dim(a.level, room_scale_);
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
// has no business asking for a tone once something else owns the pipeline.
//
// `hal::audio::tone()` carries its own duration and its own fade, so nothing here has to
// count the note out; `chime_off_us_` is kept only to hold the repeat off until the note is
// done.  A refused chime is deliberately silent: on a board with no amp fitted this fires
// every 1.5 s, and D16's answer to absence is not a log line per second.
void Ui::chime_tick() noexcept {
    if (mode_ != Mode::Volume) return;
    const uint64_t now = port::now_us();
    if (chime_off_us_ && now >= chime_off_us_) chime_off_us_ = 0;
    if (!chime_off_us_ && chime_at_us_ && now >= chime_at_us_) {
        (void)hal::audio::tone(kChimeHz, kChimeMs);
        chime_off_us_ = now + kChimeMs * 1000ull;
        chime_at_us_ = now + kChimeEveryMs * 1000ull;
    }
}

// Called once per edit -- an arm/disarm, a `chrono alarm` -- never per minute of a knob wind:
// NVS pages are not free.  The week is a string so the same key can grow a field.
void Ui::save_alarm() const noexcept {
    char wk[64];
    week_to_str(week_, wk, sizeof wk);
    (void)hal::store::set_str(kKeyWeek, wk);
    (void)hal::store::set_i32(kKeyArmed, alarm_armed_ ? 1 : 0);
}

void Ui::save_override() const noexcept {
    auto i32 = [](int64_t v) {
        return v == al::kNone || v < INT32_MIN + 1 || v > INT32_MAX ? kNvsNone
                                                                    : static_cast<int32_t>(v);
    };
    (void)hal::store::set_i32(kKeyOvrAt, i32(ovr_.at));
    (void)hal::store::set_i32(kKeyOvrDay, i32(ovr_.day));
}

void Ui::set_week(al::Week const& w) noexcept {
    {
        port::Lock lk{mx_};
        week_in_ = w;
    }
    post(AlarmCfg{-1, -1, AlarmCfg::Week});
}

bool Ui::now_local(int64_t& min, bool& dated) const noexcept {
    min = 0;
    dated = false;
    if (!chrono_) return false;
    const auto c = chrono_->snapshot();
    if (!c.valid) return false;
    min = al::local_minute(c.epoch_ms, c.tz_off_min);
    dated = c.date_valid;
    return true;
}

al::Next Ui::next_alarm() const noexcept {
    int64_t now = 0;
    bool dated = false;
    // No time at all: nothing can be placed on a calendar, but the dial still wants a minute
    // to show, and minute 0 of day 0 finds the week's first one.
    (void)now_local(now, dated);
    return al::next(week_, ovr_, after_fired(now), dated);
}

// The knob's edit (and `chrono alarm next`): the next alarm rings at `m`, once.  Anchored to
// whatever the clock reads now -- with no time at all there is no "next", but nothing rings
// without a time either (watch_alarm), and the first real time sweeps it away as stale.
void Ui::make_override(int m) noexcept {
    int64_t now = 0;
    bool dated = false;
    (void)now_local(now, dated);
    ovr_ = al::override_at(week_, after_fired(now), m, dated);
    save_override();
    if (ovr_.pending())
        CLK_LOGI(ui, "next alarm %02d:%02d, once (%s) -- the week is unchanged", m / 60, m % 60,
                 dated ? al::day_name(al::weekday(ovr_.day)) : "no date");
    else
        CLK_LOGI(ui, "next alarm %02d:%02d is the week's own -- no override", m / 60, m % 60);
}

void Ui::chime_stop() noexcept {
    if (chime_off_us_) (void)hal::audio::stop();
    chime_off_us_ = chime_at_us_ = 0;
}

// The alarm, once a tick.  Three clocks run here: is it due, is the snooze over, has it rung
// long enough.  Wall time for the first (it is a TIME, and the clock can be set under it);
// monotonic for the other two (they are durations, and must not care if it is).
void Ui::watch_alarm() noexcept {
    const uint64_t now = port::now_us();
    const auto t = tuning();
    if (mode_ == Mode::Snoozed && now >= snooze_until_us_) {
        CLK_LOGI(ui, "snooze over -- ringing again");
        enter(Mode::Ringing);
        return;
    }
    if (mode_ == Mode::Ringing && now - ring_since_us_ >= t.ring_max_min * 60'000'000ull) {
        CLK_LOGW(ui, "alarm rang %u min unanswered -- stopping",
                 static_cast<unsigned>(t.ring_max_min));
        enter(Mode::Idle);
        return;
    }
    int64_t lnow = 0;
    bool dated = false;
    if (!now_local(lnow, dated)) return;  // no time, no alarm: 07:00 of an unset clock is an uptime
    // Housekeeping first, armed or not: a one-off whose minute has gone (rung, or the clock
    // was set past it) is over, and so is the day it replaced.
    if (const auto o = al::expire(ovr_, lnow); !(o == ovr_)) {
        ovr_ = o;
        save_override();
    }
    if (!alarm_armed_ || ringing()) return;
    // Not while the alarm is being EDITED: the time under the knob sweeps past "now" on its way
    // to where it is going, and that is not the user asking to be woken.  Nothing is marked as
    // fired, so if the edit ends inside the minute it still rings.
    if (mode_ == Mode::Alarm) return;
    if (!al::due(week_, ovr_, lnow, dated)) return;
    if (lnow == fired_min_) return;
    fired_min_ = lnow;
    alarm_rung_min_ = al::min_of_day(lnow);
    CLK_LOGI(ui, "ALARM %02d:%02d fires%s", alarm_rung_min_ / 60, alarm_rung_min_ % 60,
             ovr_.at == lnow ? " (one-off)" : "");
    enter(Mode::Ringing);
}

void Ui::publish() noexcept {
    const auto t = tuning();
    const uint64_t now = port::now_us();
    const uint32_t limit = t.timeout_ms;
    const uint64_t since = now - last_input_us_;
    const auto left = static_cast<uint32_t>(since / 1000ull >= limit ? 0 : limit - since / 1000ull);
    // Read before the lock: net_owns_time() takes chrono's, and two services' mutexes held
    // in one order here and the other order there is the whole recipe for a deadlock.
    const bool locked = net_owns_time();
    int64_t now_min = 0;
    bool dated = false;
    const bool timed = now_local(now_min, dated);
    const auto nx = al::next(week_, ovr_, after_fired(now_min), dated);
    int shown = week_.min[timed && dated ? al::weekday(al::day_of(now_min)) : 0];
    if (nx.src != al::Src::None) shown = al::min_of_day(nx.at);
    if (mode_ == Mode::Alarm) shown = set_min_of_day_;  // the knob's edit, live
    port::Lock lk{mx_};
    snap_.mode = mode_;
    snap_.mode_name = name_of(mode_);
    snap_.alarm_armed = alarm_armed_;
    snap_.alarm_hour = shown / 60;
    snap_.alarm_minute = shown % 60;
    snap_.alarm_week = week_;
    snap_.alarm_next = nx.src;
    snap_.alarm_next_at = nx.at;
    snap_.alarm_next_wday = static_cast<int8_t>(
        nx.src != al::Src::None && timed && dated ? al::weekday(al::day_of(nx.at)) : -1);
    snap_.alarm_ovr = ovr_;
    snap_.volume = volume_;
    snap_.counts_per_minute = tune_.counts_per_minute;
    snap_.idle_in_ms = mode_ == Mode::Idle ? 0 : left;
    snap_.net_locked = locked;
    snap_.held_ms = sw_last_ ? static_cast<uint32_t>((now - sw_down_us_) / 1000ull) : 0;
    snap_.ringing_ms =
        mode_ == Mode::Ringing ? static_cast<uint32_t>((now - ring_since_us_) / 1000ull) : 0u;
    snap_.snooze_left_ms = mode_ == Mode::Snoozed && snooze_until_us_ > now
                               ? static_cast<uint32_t>((snooze_until_us_ - now) / 1000ull)
                               : 0u;
    snap_.knob_count = knob_last_;
    snap_.input = input_;
    snap_.brightness = tune_.brightness;
    snap_.power_ok = power_ok_;
    snap_.power = power_;
    snap_.batt_warn = batt_warn_;
    snap_.imu_ok = imu_ok_;
    snap_.imu = imu_;
    snap_.room_scale = room_scale_;
    snap_.room_lux = dimmer_.anchor_lux();
    snap_.faults = faults_;
}

// The window can close without the knob: a phone bonded, the two minutes ran out, or the
// rear toggle killed the radio.  Any of those ends the mode -- the blue row is a promise that
// a phone can pair right now, and it must not outlive the promise.  A bond gets two green
// flashes across the row, because "it worked" is the one answer the user is waiting for.
void Ui::watch_pairing() noexcept {
    if (!net_ || mode_ != Mode::Pairing) return;
    const auto n = net_->snapshot();
    if (n.pairing || n.windows == net_windows_) return;
    net_windows_ = n.windows;
    CLK_LOGI(ui, "pairing over (%s)",
             n.last_end == Net::PairEnd::Bonded ? "bonded" : "window closed");
    enter(Mode::Idle);
    if (n.last_end == Net::PairEnd::Bonded) {
        for (std::size_t i = kBell; i <= kBatt; ++i)
            arm(over_, i, domain::flash(domain::kGreen, level(), 2));
    }
}

}  // namespace clk::svc
