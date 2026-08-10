// The movement.                                               [FIRMWARE.md §6.1, D5, D7]
//
// Owns both axes of the X40.879 and the homing opto.  Nobody else touches hal::motor.
//
// What lives here is the part that carries the bugs: the trapezoidal profile, the backlash
// policy, and the homing FSM.  What does NOT live here is commutation -- the HAL is a
// velocity-controlled step generator with a stop target (see hal::motor), so this file is
// the same on the bench and in clocksim.
#pragma once

#include <cstdint>

#include "clk/ao.hpp"
#include "clk/hal/hal.hpp"
#include "clk/port.hpp"

namespace clk::svc {

class Motion final : public ActiveObject {
public:
    enum class State : uint8_t { Uninit, Homing, Idle, Moving, Fault };

    // Everything a bench session wants to change without a rebuild.  `motion tune` writes
    // these; they become NVS-backed config with `storage` (§7.5).
    struct Tuning {
        int32_t v_max = 6000;    // usteps/s  -- ~2 min of dial per second
        int32_t accel = 20000;   // usteps/s^2
        int32_t v_home = 1200;   // sweep speed: slow enough not to step over the index
        int32_t v_verify = 200;  // the quarter-speed re-approach
        int32_t backlash = 0;    // usteps of slop to take up; every move finishes clockwise
        float opto_thresh = 0.45f;
    };

    struct Snapshot {
        State state;
        const char* state_name;
        const char* phase;  // homing sub-state, "" otherwise
        int32_t hour, minute;
        int32_t target_hour, target_minute;
        bool homed;
        bool powered;
        uint32_t home_ms;  // how long the last home took, sim ms
        float opto;
        uint32_t faults;
    };

    Motion() noexcept;

    // Absolute microsteps.  There is no "step N times" in this system (§6.1).
    void goto_usteps(int32_t hour, int32_t minute, bool preview = false) noexcept;
    void nudge(hal::motor::Hand, int32_t usteps) noexcept;  // bench only: `motion step`
    void home() noexcept;
    void halt() noexcept;

    [[nodiscard]] Snapshot snapshot() const noexcept;
    [[nodiscard]] Tuning tuning() const noexcept;
    void set_tuning(Tuning const&) noexcept;

    // Gets HandState on every settle and HomeDone when homing finishes.  One subscriber is
    // enough for now (chrono); a real bus lands with §3.5.
    void subscribe(ActiveObject* ao) noexcept { sub_ = ao; }

protected:
    void on_start() override;
    void on_event(Event const&) override;
    void on_tick() override;

private:
    // §6.1's sequence.  Each hand is parked well away from the index before the other one
    // sweeps, so one sensor is enough and there is never a "which hand was that" question.
    enum class Phase : uint8_t {
        None,
        ParkMinute,   // find the index with the minute hand, then drive half a turn past it
        SweepHour,    // hour hand alone crosses the index
        ParkHour,     // hour half a turn away
        SweepMinute,  // minute hand alone crosses the index
        Verify,       // back off, re-approach at quarter speed, both edges must agree
        Done,
    };

    struct Axis {
        hal::motor::Hand hand;
        int32_t target = 0;
        int32_t via = 0;   // backlash undershoot; == target when the move is already CW
        bool leg2 = true;  // false while still heading for `via`
        bool active = false;
        int32_t v = 0;
    };

    void plan(Axis&, int32_t target) noexcept;
    bool step_axis(Axis&, uint32_t dt_ms) noexcept;  // true while still moving
    void power(bool on) noexcept;
    void publish() noexcept;
    void enter(Phase) noexcept;
    void run_homing(float opto) noexcept;
    void finish_home() noexcept;
    void fail(const char* why) noexcept;

    [[nodiscard]] int32_t pos(hal::motor::Hand h) const noexcept {
        return hal::motor::state(h).pos;
    }

    mutable port::Mutex mx_;
    Snapshot snap_{};
    Tuning tune_{};

    Axis hour_{hal::motor::Hand::Hour};
    Axis min_{hal::motor::Hand::Minute};

    State state_ = State::Uninit;
    Phase phase_ = Phase::None;
    bool homed_ = false;
    bool powered_ = false;
    uint64_t idle_since_us_ = 0;
    uint64_t last_tick_us_ = 0;
    uint64_t home_start_us_ = 0;
    uint64_t phase_start_us_ = 0;
    float opto_ = 0.0f;
    bool opto_high_ = false;
    int32_t edge_pos_ = 0;
    int32_t verify_pass_ = 0;
    uint32_t faults_ = 0;
    // The last target asked for, re-issued after a home completes: whatever the clock wanted
    // while the hands were busy finding zero is still what it wants afterwards.
    int32_t want_h_ = 0, want_m_ = 0;
    bool want_valid_ = false;
    ActiveObject* sub_ = nullptr;
};

Motion& motion() noexcept;

}  // namespace clk::svc
