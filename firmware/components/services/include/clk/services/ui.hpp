// The knob HSM and every emitter.                             [FIRMWARE.md §6.6, README §12]
//
// Owns the encoder (PCNT diff every 20 ms), ENC_SW, the seven pixels and the wake light.
//
// One press steps the mode, a turn edits the lit mode, five seconds without input drops
// back to Idle, and a ten-second hold opens BLE pairing.  The modes are NAMED AFTER THE
// ICONS on the plate -- `bell` arms the alarm, `alarm` sets its time, `clock` sets the time
// -- because the icon is the only label the user ever sees.
#pragma once

#include <cstdint>

#include "clk/ao.hpp"
#include "clk/domain/anim.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"

namespace clk::svc {

class Ui final : public ActiveObject {
public:
    // README §12, in the order a press visits them.  Battery is status-only and is skipped
    // by the cycle; Pairing is off the cycle entirely and is reached by holding.
    enum class Mode : uint8_t { Idle, Bell, Alarm, Clock, Volume, Pairing };

    struct Snapshot {
        Mode mode;
        const char* mode_name;
        bool alarm_armed;
        int alarm_hour, alarm_minute;
        uint8_t volume;
        int32_t counts_per_minute;
        uint32_t idle_in_ms;  // time left before the timeout drops us to Idle
        // The network owns the time (radio on + provisioned + synced at least once), so
        // `clock` refuses rather than letting the knob overwrite what SNTP will restore.
        bool net_locked;
        uint32_t held_ms;  // how long ENC_SW has been down right now; 0 when it is up
    };

    struct Tuning {
        int32_t counts_per_minute = 4;  // 256 counts/rev; 4 -> one minute per detent
        // The fast/slow curve, and it belongs to the VOLUME alone (§6.6d, 2026-08-16).  A
        // turn arrives as counts-per-20 ms-poll: at or under `slow_max` a count is worth
        // exactly one count, at `fast_at` it is worth `accel_factor`, straight line between.
        // Setting a TIME does not use it -- a poll worth two hours is a minute hand the dial
        // cannot draw, and the setting is paced to the movement instead (Ui::drain_setting).
        int32_t slow_max = 4;
        int32_t fast_at = 24;
        int32_t accel_factor = 12;
        // THE timeout, and there is only one: every mode drops to Idle five seconds after the
        // last input, pairing included.  A mode with its own number is a second rule to learn
        // about a control that has no labels.
        uint32_t timeout_ms = 5000;
        uint32_t long_press_ms = 800;    // commit and drop to Idle
        uint32_t pair_press_ms = 10000;  // ... and this far in, BLE pairing instead
        uint8_t brightness = 60;         // percent, perceptual (gamma is applied after it)
        uint8_t arm_deadband = 2;        // counts before a turn in `bell` means anything
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
    [[nodiscard]] domain::AnimCfg anim_cfg() const noexcept;
    void set_anim_cfg(domain::AnimCfg const&) noexcept;

protected:
    void on_start() override;
    void on_event(Event const&) override;
    void on_tick() override;

private:
    void poll_knob() noexcept;
    void poll_tap() noexcept;
    void watch_battery() noexcept;
    void enter(Mode) noexcept;
    void rotate(int32_t counts) noexcept;
    void press(uint32_t held_ms) noexcept;
    void commit_clock() noexcept;
    void show_hands(int dir = 0) noexcept;  // dir: which way the knob just turned
    // Setting a time is paced to what the movement can actually draw: counts arrive at
    // rotate() and are spent here, one minute at a time, no faster than the hands run --
    // and what arrives faster than that is DROPPED rather than paid out after the knob has
    // stopped, because a dial that carries on winding is a time nobody chose (§6.6d).
    void drain_setting() noexcept;
    [[nodiscard]] uint32_t pace_ms() const noexcept;
    void publish() noexcept;
    [[nodiscard]] bool net_owns_time() const noexcept;
    [[nodiscard]] int32_t gain_for(int32_t magnitude) const noexcept;

    // ---- light ----------------------------------------------------------------------
    // Two layers per pixel.  `base_` is what the MODE wants and changes when the mode or
    // the thing it shows changes; `over_` is a transient that outranks it and hands the
    // pixel back when it finishes -- the tap acknowledgement, the refusal burst, and one
    // day the fault code.  Rendering is one pass over both, every tick.
    void cue() noexcept;  // recompute base_
    void arm(domain::Anim* layer, std::size_t i, domain::Anim) noexcept;
    void fade_out(std::size_t i) noexcept;
    void render() noexcept;
    [[nodiscard]] domain::Anim alarm_cue() const noexcept;  // the bell/alarm rule
    [[nodiscard]] uint8_t level() const noexcept;           // Tuning::brightness, 0..255

    void chime_tick() noexcept;
    void chime_stop() noexcept;

    mutable port::Mutex mx_;
    Snapshot snap_{};
    Tuning tune_{};
    domain::AnimCfg anim_{};

    Motion* motion_ = nullptr;
    Chrono* chrono_ = nullptr;

    Mode mode_ = Mode::Idle;
    uint64_t last_input_us_ = 0;

    domain::Anim base_[hal::pixels::kCount]{};
    domain::Anim over_[hal::pixels::kCount]{};
    // What we last wrote to the chain.  `ui` only touches a pixel when its OWN rendering
    // changes, which is what lets `ui led` (§9.3) hold the chain while nothing is animating
    // -- a bring-up command that is overwritten 20 ms later is not a bring-up command.
    hal::pixels::Rgbw shown_[hal::pixels::kCount]{};
    bool force_write_ = true;

    int32_t knob_last_ = 0;
    bool sw_last_ = false;
    uint64_t sw_down_us_ = 0;
    bool pair_armed_ = false;  // the hold already became pairing; the release is spent

    // Held here until `storage` exists.  An alarm the user set should survive a reboot; for
    // now it survives as long as clocksim runs, which is enough to tune the interaction.
    bool alarm_armed_ = false;
    int alarm_min_of_day_ = 7 * 60;
    int set_min_of_day_ = 0;  // what the knob is editing in Alarm / Clock
    // Counts the knob has delivered and the setting has not spent yet.  Not an optimisation:
    // a turn arrives as a stream of small deltas, and dividing each one on its own discards
    // the remainder EVERY time.  Sub-minute only in `alarm` and `clock` -- whole minutes are
    // either spent or dropped on the spot, never carried (Ui::drain_setting).
    int32_t counts_resid_ = 0;
    uint64_t last_unit_us_ = 0;  // when the last minute of setting was spent
    int32_t arm_resid_ = 0;      // and the same for the bell's direction deadband
    uint8_t volume_ = 40;
    uint64_t chime_at_us_ = 0;   // next chime starts
    uint64_t chime_off_us_ = 0;  // ... and the current one ends
    // The cell warning is polled, not evented -- there is no producer of PowerState yet.
    bool batt_warn_ = false;
    uint8_t power_div_ = 0;
    // Likewise the top tap, until the BNO085 driver exists to post it.  The first poll only
    // establishes the baseline: whatever the counter already read is not a tap the user made.
    uint16_t taps_last_ = 0;
    uint8_t tap_div_ = 0;
};

Ui& ui() noexcept;

}  // namespace clk::svc
