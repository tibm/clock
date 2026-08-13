#include "clk/services/ui.hpp"

#include <cstdlib>

#include "clk/domain/hand.hpp"
#include "clk/log.hpp"

namespace clk::svc {
namespace {

using hal::pixels::Rgbw;

constexpr uint32_t kTickMs = 20;          // §6.6: PCNT is polled and diffed every 20 ms
constexpr uint32_t kTapAckMs = 400;       // long enough to be seen, short enough not to linger
constexpr uint8_t kPowerEveryTicks = 12;  // ~4 Hz; it is a battery, not a trigger
constexpr uint8_t kTapEveryTicks = 2;     // 10 Hz: a snooze tap must not feel laggy
constexpr uint8_t kLowBattPct = 20;

// Chain order is dial first (§9.2): 0-1 on-PCB dial wash, 2-6 the status row through J12.
constexpr std::size_t kBell = 2, kAlarmClock = 3, kClock = 4, kVol = 5, kBatt = 6;

const char* name_of(Ui::Mode m) noexcept {
    switch (m) {
        case Ui::Mode::Idle:
            return "idle";
        case Ui::Mode::Alarm:
            return "alarm";
        case Ui::Mode::SetAlarm:
            return "setalarm";
        case Ui::Mode::SetClock:
            return "setclock";
        case Ui::Mode::Volume:
            return "volume";
    }
    return "?";
}

Rgbw scale(Rgbw c, int pct) noexcept {
    auto s = [pct](uint8_t v) { return static_cast<uint8_t>(v * pct / 100); };
    return {s(c.r), s(c.g), s(c.b), s(c.w)};
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
    paint();
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

void Ui::set_mode(Mode m) noexcept { post(ModeSet{static_cast<uint8_t>(m)}); }

void Ui::on_event(Event const& e) {
    if (const auto* m = as<ModeSet>(e)) return enter(static_cast<Mode>(m->mode));
    if (const auto* d = as<KnobDelta>(e)) return rotate(d->counts);
    if (const auto* p = as<KnobPress>(e)) {
        if (!p->down) press(p->held_ms);
        return;
    }
    if (as<Tap>(e)) {
        // Tap-to-snooze (README §12).  Until the alarm exists it is a visible acknowledgement,
        // which is still the interaction worth tuning: a tap must feel like it did something.
        //
        // Marking the pixels dirty here is what it must NOT do.  paint() renders the MODE,
        // and in Idle the mode is "everything off" -- so the flash was set and then painted
        // over by the very next tick, twenty milliseconds later.  Hold the acknowledgement
        // instead, and let on_tick() take the pixels back when it has been seen.
        CLK_LOGI(ui, "tap");
        hal::pixels::set(kBell, {0, 0, 0, 120});
        hal::pixels::refresh();
        ack_until_us_ = port::now_us() + kTapAckMs * 1000ull;
        last_input_us_ = port::now_us();
    }
}

void Ui::on_tick() {
    poll_knob();
    poll_tap();
    watch_battery();

    const auto t = tuning();
    if (mode_ != Mode::Idle && port::now_us() - last_input_us_ > t.timeout_ms * 1000ull) {
        CLK_LOGI(ui, "timeout -> idle (settings kept)");
        enter(Mode::Idle);
    }
    // A tap acknowledgement owns the pixels until it lapses; any real input outranks it,
    // because a mode change the user just asked for matters more than a flash.
    if (ack_until_us_ && port::now_us() >= ack_until_us_) {
        ack_until_us_ = 0;
        dirty_ = true;
    }
    if (dirty_) {
        paint();
        dirty_ = false;
        ack_until_us_ = 0;
    }
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

// The low-cell warning is the one thing on the pixels that no INPUT causes, so nothing was
// ever marking it dirty: the cell could sag to 5 % and the pixel stayed dark until the next
// unrelated knob turn happened to repaint.  Poll it slowly -- on the board this is an ADC
// read, and four times a second is plenty for a battery.
void Ui::watch_battery() noexcept {
    if (++power_div_ < kPowerEveryTicks) return;
    power_div_ = 0;
    const auto p = hal::power::read();
    const bool warn = p.ok() && p.v.soc_pct < kLowBattPct && !p.v.plugged;
    if (warn != batt_warn_) {
        batt_warn_ = warn;
        dirty_ = true;
    }
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
}

void Ui::enter(Mode m) noexcept {
    mode_ = m;
    last_input_us_ = port::now_us();
    dirty_ = true;
    // Part of a turn left over from the last mode is not part of this one.
    counts_resid_ = 0;

    if (m == Mode::SetAlarm) set_min_of_day_ = alarm_min_of_day_;
    if (m == Mode::SetClock && chrono_) {
        const auto c = chrono_->snapshot();
        set_min_of_day_ = c.hour * 60 + c.minute;
    }
    if (m == Mode::Idle) {
        // "Zero emission when idle is a hard invariant" (§6.6, R2/R6).
        hal::pixels::set_all(Rgbw{});
        hal::pixels::refresh();
        if (chrono_) chrono_->set_follow(true);
    } else if (m == Mode::SetClock || m == Mode::SetAlarm) {
        if (chrono_) chrono_->set_follow(m != Mode::SetClock);
    }
    preview_hands();
    CLK_LOGI(ui, "mode %s", name_of(m));
}

// The whole point of a 64 CPR optical encoder with no detent: sensitivity is a firmware
// mapping, and an acceleration curve lets one flick cover twelve hours without making a
// slow turn coarse.
void Ui::rotate(int32_t counts) noexcept {
    if (counts == 0) return;
    last_input_us_ = port::now_us();
    dirty_ = true;

    if (mode_ == Mode::Idle) return;  // a turn in Idle wakes nothing; press first

    const auto t = tuning();
    const int32_t mag = std::abs(counts);
    const int32_t gain = mag > t.accel_threshold ? t.accel_factor : 1;
    const int32_t per = t.counts_per_minute ? t.counts_per_minute : 1;

    // Carry the remainder.  A hand on the knob does not deliver its counts in one lump: PCNT
    // is read every tick and the UI streams a dragged knob as one- and two-count deltas, so
    // dividing each delta on its own threw away the whole turn whenever counts_per_minute
    // was more than 1 -- the knob moved nothing at all, at any speed, unless you flicked it.
    counts_resid_ += counts * gain;
    const int32_t minutes = counts_resid_ / per;  // truncates toward zero, both signs
    counts_resid_ -= minutes * per;
    if (minutes == 0) return;

    switch (mode_) {
        case Mode::Alarm:
            // Rotating in the bell mode arms/disarms rather than editing (README §12).
            alarm_armed_ = minutes > 0;
            break;
        case Mode::SetAlarm:
            set_min_of_day_ = wrap_day(set_min_of_day_ + minutes);
            alarm_min_of_day_ = set_min_of_day_;
            break;
        case Mode::SetClock:
            set_min_of_day_ = wrap_day(set_min_of_day_ + minutes);
            break;
        case Mode::Volume: {
            const int v = static_cast<int>(volume_) + minutes;
            volume_ = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
            hal::audio::set_volume_pct(volume_);
            break;
        }
        default:
            break;
    }
    preview_hands();
}

void Ui::press(uint32_t held_ms) noexcept {
    last_input_us_ = port::now_us();
    dirty_ = true;
    const auto t = tuning();

    if (held_ms >= t.long_press_ms) {
        CLK_LOGI(ui, "long press -> idle");
        if (mode_ == Mode::SetClock && chrono_) {
            // Committing a clock set is the one place the knob writes the time.
            const auto c = chrono_->snapshot();
            const int64_t day = c.epoch_ms - ((c.hour * 60 + c.minute) * 60 + c.second) * 1000ll;
            chrono_->set_time(day + set_min_of_day_ * 60'000ll);
        }
        enter(Mode::Idle);
        return;
    }

    switch (mode_) {
        case Mode::Idle:
            enter(Mode::Alarm);
            break;
        case Mode::Alarm:
            enter(Mode::SetAlarm);
            break;
        case Mode::SetAlarm:
            enter(Mode::SetClock);
            break;
        case Mode::SetClock:
            if (chrono_) {
                const auto c = chrono_->snapshot();
                const int64_t day =
                    c.epoch_ms - ((c.hour * 60 + c.minute) * 60 + c.second) * 1000ll;
                chrono_->set_time(day + set_min_of_day_ * 60'000ll);
            }
            enter(Mode::Volume);
            break;
        case Mode::Volume:
            enter(Mode::Idle);
            break;
    }
}

// The hands ARE the readout in the set modes (README §5): they show the alarm time in the
// bell mode and track the knob live while setting.
void Ui::preview_hands() noexcept {
    if (!motion_) return;
    int show = -1;
    switch (mode_) {
        case Mode::Alarm:
            show = alarm_min_of_day_;
            break;
        case Mode::SetAlarm:
        case Mode::SetClock:
            show = set_min_of_day_;
            break;
        default:
            break;
    }
    if (show < 0) return;
    const auto p = domain::for_time(show / 60, show % 60);
    motion_->goto_usteps(p.hour, p.minute, true);
}

void Ui::paint() noexcept {
    const auto t = tuning();
    const int b = t.brightness;
    hal::pixels::set_all(Rgbw{});

    switch (mode_) {
        case Mode::Idle:
            break;  // zero emission when idle -- hard invariant
        case Mode::Alarm:
            hal::pixels::set(kBell,
                             scale(alarm_armed_ ? Rgbw{255, 0, 0, 0} : Rgbw{0, 0, 0, 255}, b));
            break;
        case Mode::SetAlarm:
            hal::pixels::set(kAlarmClock, scale({0, 0, 0, 255}, b));
            break;
        case Mode::SetClock:
            hal::pixels::set(kClock, scale({0, 0, 0, 255}, b));
            break;
        case Mode::Volume: {
            // The pixel doubles as the level readout: dim at 0, full at 100.
            const int pct = 10 + volume_ * 90 / 100;
            hal::pixels::set(kVol, scale({0, 0, 0, 255}, b * pct / 100));
            break;
        }
    }

    if (batt_warn_) hal::pixels::set(kBatt, scale({255, 60, 0, 0}, b));
    hal::pixels::refresh();
}

void Ui::publish() noexcept {
    const auto t = tuning();
    const uint64_t since = port::now_us() - last_input_us_;
    const auto left =
        static_cast<uint32_t>(since / 1000ull >= t.timeout_ms ? 0 : t.timeout_ms - since / 1000ull);
    port::Lock lk{mx_};
    snap_.mode = mode_;
    snap_.mode_name = name_of(mode_);
    snap_.alarm_armed = alarm_armed_;
    snap_.alarm_hour = alarm_min_of_day_ / 60;
    snap_.alarm_minute = alarm_min_of_day_ % 60;
    snap_.volume = volume_;
    snap_.counts_per_minute = tune_.counts_per_minute;
    snap_.idle_in_ms = mode_ == Mode::Idle ? 0 : left;
}

}  // namespace clk::svc
