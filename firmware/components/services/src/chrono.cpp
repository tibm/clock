#include "clk/services/chrono.hpp"

#include "clk/domain/hand.hpp"
#include "clk/log.hpp"

namespace clk::svc {
namespace {

constexpr uint32_t kTickMs = 250;  // four times a second is plenty for a minute hand

struct Hms {
    int h, m, s;
};

// Local time == UTC for now; TZ/DST arrive with §6.4 and change only this function.
Hms hms_of(int64_t epoch_ms) noexcept {
    int64_t secs = epoch_ms / 1000;
    int64_t day = secs % 86400;
    if (day < 0) day += 86400;
    return {static_cast<int>(day / 3600), static_cast<int>((day % 3600) / 60),
            static_cast<int>(day % 60)};
}

}  // namespace

Chrono::Chrono() noexcept : ActiveObject({"chrono", 12, 4096, kTickMs}) {}

Chrono& chrono() noexcept {
    static Chrono c;
    return c;
}

void Chrono::on_start() {
    mono_base_us_ = port::now_us();
    CLK_LOGI(chrono, "up; time not set (no RTC here -- `chrono time set`)");
}

int64_t Chrono::now_epoch_ms() const noexcept {
    port::Lock lk{mx_};
    return snap_.epoch_ms;
}

Chrono::Snapshot Chrono::snapshot() const noexcept {
    port::Lock lk{mx_};
    return snap_;
}

// Called from OTHER threads -- `ui` on every mode change, `cli` on `chrono follow`.  All
// three fields are read by push_target() on chrono's own thread, so all three are written
// under the mutex.  They were not, and ThreadSanitizer called it exactly right: a stale
// `follow_` leaves the clock driving the hands through a knob preview, which is a bug you
// would chase in `ui` for an hour (FIRMWARE.md §16).
void Chrono::set_follow(bool on) noexcept {
    port::Lock lk{mx_};
    snap_.follow = on;
    follow_ = on;
    last_h_ = last_m_ = -1;  // force a push on the next tick
}

void Chrono::set_net(bool provisioned, bool synced) noexcept {
    port::Lock lk{mx_};
    // Straight into the snapshot: on_tick rewrites the time fields and leaves these alone,
    // and nothing inside chrono reads them -- they exist to be asked about.
    snap_.net_provisioned = provisioned;
    snap_.net_synced = synced;
}

void Chrono::set_steps_per_minute(int n) noexcept {
    port::Lock lk{mx_};
    steps_per_minute_ = n < 1 ? 1 : (n > 60 ? 60 : n);
    last_h_ = last_m_ = -1;  // the current position is quantised differently now
}

int Chrono::steps_per_minute() const noexcept {
    port::Lock lk{mx_};
    return steps_per_minute_;
}

void Chrono::on_event(Event const& e) {
    if (const auto* t = as<TimeChanged>(e)) {
        epoch_base_ms_ = t->epoch_ms;
        mono_base_us_ = port::now_us();
        valid_ = true;
        const auto hm = hms_of(t->epoch_ms);
        CLK_LOGI(chrono, "time set to %02d:%02d:%02d", hm.h, hm.m, hm.s);
        push_target(true);
        return;
    }
    if (const auto* h = as<HomeDone>(e)) {
        // A fresh zero means the hands are somewhere we did not ask for; put them back on
        // the time rather than waiting for the minute to roll over.
        CLK_LOGI(chrono, "motion homed (%s)", h->ok ? "ok" : "failed");
        if (h->ok) push_target(true);
    }
}

void Chrono::on_tick() {
    const int64_t now_ms =
        epoch_base_ms_ + static_cast<int64_t>((port::now_us() - mono_base_us_) / 1000ull);
    const auto hm = hms_of(now_ms);
    int steps;
    {
        port::Lock lk{mx_};
        steps = steps_per_minute_;
    }
    const auto p = domain::for_time(hm.h, hm.m, hm.s, steps);
    {
        port::Lock lk{mx_};
        snap_.epoch_ms = now_ms;
        snap_.hour = hm.h;
        snap_.minute = hm.m;
        snap_.second = hm.s;
        snap_.valid = valid_;
        snap_.follow = follow_;
        snap_.target_hour = p.hour;
        snap_.target_minute = p.minute;
    }
    push_target(false);
}

// Absolute targets, only when they change: the movement is idle >99 % of the time (§6.1) and
// re-sending the same position every tick would keep the coils alive for nothing.
void Chrono::push_target(bool force) noexcept {
    if (!motion_) return;
    int32_t h, m;
    {
        // One acquisition, and snap_ read directly rather than through snapshot(): the
        // decision and the fields it is made from have to come from the same instant, and
        // `port::Lock` is not recursive.
        port::Lock lk{mx_};
        if (!valid_ || !follow_) return;
        h = snap_.target_hour;
        m = snap_.target_minute;
        if (!force && h == last_h_ && m == last_m_) return;
        last_h_ = h;
        last_m_ = m;
    }
    motion_->goto_usteps(h, m);  // outside the lock: it posts to another AO's queue
}

}  // namespace clk::svc
