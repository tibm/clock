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
    };

    Chrono() noexcept;

    void set_time(int64_t epoch_ms) noexcept { post(TimeChanged{epoch_ms}); }
    void set_follow(bool on) noexcept;
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
    int32_t last_h_ = -1, last_m_ = -1;
};

Chrono& chrono() noexcept;

}  // namespace clk::svc
