#include "clk/services/ui.hpp"

#include <cstdlib>

#include "clk/domain/hand.hpp"
#include "clk/log.hpp"

namespace clk::svc {
namespace {

using hal::pixels::Rgbw;

constexpr uint32_t kTickMs = 20;  // §6.6: PCNT is polled and diffed every 20 ms

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
        CLK_LOGI(ui, "tap");
        hal::pixels::set(kBell, {0, 0, 0, 120});
        hal::pixels::refresh();
        dirty_ = true;
        last_input_us_ = port::now_us();
    }
}

void Ui::on_tick() {
    poll_knob();

    const auto t = tuning();
    if (mode_ != Mode::Idle && port::now_us() - last_input_us_ > t.timeout_ms * 1000ull) {
        CLK_LOGI(ui, "timeout -> idle (settings kept)");
        enter(Mode::Idle);
    }
    if (dirty_) {
        paint();
        dirty_ = false;
    }
    publish();
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
    const int32_t minutes = (counts * gain) / (t.counts_per_minute ? t.counts_per_minute : 1);
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

    const auto p = hal::power::read();
    if (p.ok() && p.v.soc_pct < 20 && !p.v.plugged) {
        hal::pixels::set(kBatt, scale({255, 60, 0, 0}, b));
    }
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
