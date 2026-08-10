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

void Chrono::set_follow(bool on) noexcept {
    {
        port::Lock lk{mx_};
        snap_.follow = on;
    }
    follow_ = on;
    last_h_ = last_m_ = -1;  // force a push on the next tick
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
    const auto p = domain::for_time(hm.h, hm.m, hm.s);
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
    if (!valid_ || !follow_ || !motion_) return;
    const auto s = snapshot();
    if (!force && s.target_hour == last_h_ && s.target_minute == last_m_) return;
    last_h_ = s.target_hour;
    last_m_ = s.target_minute;
    motion_->goto_usteps(s.target_hour, s.target_minute);
}

}  // namespace clk::svc
