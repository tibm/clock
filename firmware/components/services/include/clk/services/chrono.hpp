// The time authority.                                          [FIRMWARE.md §6.4, §7.1]
//
// Minimal on purpose: a monotonic wall clock and the hand targets that follow from it.  The
// alarm table, TZ/DST and the SNTP re-home policy land on top of this shape without changing
// it -- what matters now is that exactly one thing decides what time it is and exactly one
// thing turns that into a HandTarget.
#pragma once

#include <cstdint>

#include "clk/ao.hpp"
#include "clk/services/motion.hpp"

namespace clk::svc {

class Chrono final : public ActiveObject {
public:
    struct Snapshot {
        int64_t epoch_ms;
        int hour, minute, second;
        bool valid;   // false until somebody sets it -- there is no RTC in clocksim
        bool follow;  // are the hands tracking the clock?
        int32_t target_hour, target_minute;
        // Who owns the time.  `ui` refuses to let the knob set the clock when the network
        // does (README §12), because SNTP would overwrite it at the next sync and the user
        // would be left thinking the knob is broken.
        bool net_provisioned;  // Wi-Fi credentials are stored
        bool net_synced;       // ... and SNTP has landed at least once
    };

    Chrono() noexcept;

    void set_time(int64_t epoch_ms) noexcept { post(TimeChanged{epoch_ms}); }
    // Reported by `net` when §6.7 lands; `chrono net` writes it today so the refusal is
    // reachable on the bench and in clocksim.  Two facts, not a state machine: whether the
    // radios are actually up is the rear toggle's business and `ui` reads that itself.
    void set_net(bool provisioned, bool synced) noexcept;
    void set_follow(bool on) noexcept;
    // How many distinct positions the hands take per minute of wall time: 1 ticks once a
    // minute, 60 moves every second.  A rendering choice, not a timekeeping one -- the clock
    // itself is unaffected, only how often it asks the hands to move.
    void set_steps_per_minute(int n) noexcept;
    [[nodiscard]] int steps_per_minute() const noexcept;
    [[nodiscard]] Snapshot snapshot() const noexcept;
    [[nodiscard]] int64_t now_epoch_ms() const noexcept;

    void bind(Motion* m) noexcept { motion_ = m; }

protected:
    void on_start() override;
    void on_event(Event const&) override;
    void on_tick() override;

private:
    void push_target(bool force) noexcept;

    mutable port::Mutex mx_;
    Snapshot snap_{};
    Motion* motion_ = nullptr;

    // Wall time is an offset from the monotonic base, so warping sim time warps the clock
    // with it and there is no second time source to disagree.
    int64_t epoch_base_ms_ = 0;
    uint64_t mono_base_us_ = 0;
    bool valid_ = false;
    bool follow_ = true;
    int steps_per_minute_ = 60;  // one position per second: a sweep, not a tick
    int32_t last_h_ = -1, last_m_ = -1;
};

Chrono& chrono() noexcept;

}  // namespace clk::svc
