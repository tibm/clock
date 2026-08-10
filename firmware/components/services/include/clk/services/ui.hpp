// The knob HSM and every emitter.                             [FIRMWARE.md §6.6, README §12]
//
// Owns the encoder (PCNT diff every 20 ms), ENC_SW, the seven pixels and the wake light.
// Implements README §12's mode cycle: press steps through the status LEDs, rotate edits the
// lit mode, five seconds without rotation drops back to Idle.
#pragma once

#include <cstdint>

#include "clk/ao.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"

namespace clk::svc {

class Ui final : public ActiveObject {
public:
    // README §12, in order.  Battery is status-only and is skipped by the press cycle.
    enum class Mode : uint8_t { Idle, Alarm, SetAlarm, SetClock, Volume };

    struct Snapshot {
        Mode mode;
        const char* mode_name;
        bool alarm_armed;
        int alarm_hour, alarm_minute;
        uint8_t volume;
        int32_t counts_per_minute;
        uint32_t idle_in_ms;  // time left before the 5 s timeout drops us to Idle
    };

    struct Tuning {
        int32_t counts_per_minute = 4;  // 256 counts/rev; 4 -> a turn is an hour
        int32_t accel_threshold = 12;   // counts in one 20 ms poll before the curve kicks in
        int32_t accel_factor = 6;
        uint32_t timeout_ms = 5000;
        uint32_t long_press_ms = 800;
        uint8_t brightness = 60;  // percent, before gamma
    };

    Ui() noexcept;

    void bind(Motion* m, Chrono* c) noexcept {
        motion_ = m;
        chrono_ = c;
    }
    void set_mode(Mode) noexcept;
    [[nodiscard]] Snapshot snapshot() const noexcept;
    [[nodiscard]] Tuning tuning() const noexcept;
    void set_tuning(Tuning const&) noexcept;

protected:
    void on_start() override;
    void on_event(Event const&) override;
    void on_tick() override;

private:
    void poll_knob() noexcept;
    void enter(Mode) noexcept;
    void rotate(int32_t counts) noexcept;
    void press(uint32_t held_ms) noexcept;
    void paint() noexcept;
    void preview_hands() noexcept;
    void publish() noexcept;

    mutable port::Mutex mx_;
    Snapshot snap_{};
    Tuning tune_{};

    Motion* motion_ = nullptr;
    Chrono* chrono_ = nullptr;

    Mode mode_ = Mode::Idle;
    uint64_t last_input_us_ = 0;

    int32_t knob_last_ = 0;
    bool sw_last_ = false;
    uint64_t sw_down_us_ = 0;

    // Held here until `storage` exists.  An alarm the user set should survive a reboot; for
    // now it survives as long as clocksim runs, which is enough to tune the interaction.
    bool alarm_armed_ = false;
    int alarm_min_of_day_ = 7 * 60;
    int set_min_of_day_ = 0;  // what the knob is editing in SetAlarm / SetClock
    uint8_t volume_ = 40;
    bool dirty_ = true;
};

Ui& ui() noexcept;

}  // namespace clk::svc
