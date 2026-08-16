// Hand arithmetic.                                            [FIRMWARE.md §7.2, §11.1]
//
// Pure: no HAL, no IDF, no state.  This is where the wrap-at-12:00 and shortest-path bugs
// would live, so it is the part that gets exhaustively tested (all 43 200 minute positions,
// §11.1) rather than eyeballed on a bench.
//
// Convention, everywhere: 0 microsteps = 12 o'clock, increasing CLOCKWISE.  Positions are
// unwrapped int32 so "go the long way round" is expressible; only the modulo matters to the
// dial, and normalise() is the one place that knows it.
#pragma once

#include <cstdint>

#include "clk/hal/hal.hpp"

namespace clk::domain {

inline constexpr int32_t kRev = hal::motor::kUstepsPerRev;

// Positive modulo -- C++'s % is not, and a hand at -1 microstep is at 17 279, not at -1.
[[nodiscard]] constexpr int32_t normalise(int32_t usteps) noexcept {
    const int32_t m = usteps % kRev;
    return m < 0 ? m + kRev : m;
}

[[nodiscard]] constexpr float to_deg(int32_t usteps) noexcept {
    return static_cast<float>(normalise(usteps)) * 360.0f / static_cast<float>(kRev);
}

[[nodiscard]] constexpr int32_t from_deg(float deg) noexcept {
    const float t = deg * static_cast<float>(kRev) / 360.0f;
    return normalise(static_cast<int32_t>(t + (t < 0 ? -0.5f : 0.5f)));
}

// Where the hands belong for a wall-clock time.  The hour hand moves continuously, as a
// real one does: at 07:30 it sits halfway between 7 and 8, not on the 7.
struct Position {
    int32_t hour;
    int32_t minute;
};

// `steps_per_minute` is how many DISTINCT positions the hands take in a minute -- the
// difference between a clock that ticks and one that sweeps.  1 is a hand that jumps once a
// minute and is otherwise still; 60 is one that moves every second, which is as fine as a
// wall clock carrying whole seconds can be.  It quantises the TIME, not each hand, so both
// hands step together and the hour hand keeps its proper fraction of the way to the next
// hour -- quantising them separately would let the minute hand sit on :00 while the hour hand
// had already crept off the hour, which is the thing that reads as broken.
[[nodiscard]] constexpr Position for_time(int hour24, int minute, int second = 0,
                                          int steps_per_minute = 60) noexcept {
    const int32_t n = steps_per_minute < 1 ? 1 : (steps_per_minute > 60 ? 60 : steps_per_minute);
    const int32_t sec_of_half_day =
        static_cast<int32_t>((hour24 % 12) * 3600 + minute * 60 + second);
    const int32_t sec_of_hour = static_cast<int32_t>(minute * 60 + second);
    // floor to the step boundary: ticks of (60/n) seconds, counted without ever forming the
    // fraction, so n need not divide 60.
    const int64_t steps_half_day = static_cast<int64_t>(sec_of_half_day) * n / 60;
    const int64_t steps_hour = static_cast<int64_t>(sec_of_hour) * n / 60;
    const int64_t h = steps_half_day * kRev / (720 * n);
    const int64_t m = steps_hour * kRev / (60 * n);
    return {static_cast<int32_t>(h), static_cast<int32_t>(m)};
}

// The shortest signed move from `from` to `to`, in (-kRev/2, +kRev/2].
[[nodiscard]] constexpr int32_t shortest(int32_t from, int32_t to) noexcept {
    int32_t d = normalise(to) - normalise(from);
    if (d > kRev / 2) d -= kRev;
    if (d <= -kRev / 2) d += kRev;
    return d;
}

// The signed move from `from` to `to` that turns the way `dir` says -- +1 clockwise, -1
// anticlockwise, 0 "whichever is shorter".  Never more than one revolution, and a hand
// already on its target never sets off round the dial to arrive back where it is.
//
// Shortest is right for a CLOCK, which follows a value that moves slowly: 11:59 -> 12:00 is
// one minute forward however you write it down.  It is wrong for a KNOB, where the user is
// turning something and watching the hands answer: 12:00 -> 12:31 is *shorter* going
// backwards, and a minute hand that runs backwards under a clockwise turn is the bug this
// exists to prevent (§6.6e).
[[nodiscard]] constexpr int32_t directed(int32_t from, int32_t to, int dir) noexcept {
    if (dir == 0) return shortest(from, to);
    const int32_t cw = normalise(to - from);  // 0 .. kRev-1, always the clockwise way
    return dir > 0 ? cw : (cw == 0 ? 0 : cw - kRev);
}

// Chase an UNWRAPPED setpoint: go the way it actually lies, and drop whole revolutions.
//
// The knob can wind a value three turns ahead of a hand that moves at 6000 usteps/s.  Winding
// those three turns out is neither possible nor wanted -- they are invisible, a hand at 12:20
// looks the same on every one of them -- but reversing to save the last 29 minutes of travel
// is very visible indeed.  So: the direction of the error, the magnitude modulo a revolution.
[[nodiscard]] constexpr int32_t chase(int32_t from, int32_t to) noexcept {
    return directed(from, to, to == from ? 0 : (to > from ? 1 : -1));
}

// Where to actually drive, given the backlash policy: every move FINISHES clockwise (§6.1),
// so an anticlockwise move undershoots by `backlash` and comes back up through the slop.
// Returns the two legs; leg 2 == target when no doubling back is needed.
struct Approach {
    int32_t via;     // drive here first (== target if the move is already clockwise)
    int32_t target;  // and finish here, always arriving clockwise
};

// Given the move itself, rather than a destination -- `directed`/`chase` have already decided
// which way round, and re-deriving that from the two endpoints here would throw it away.
[[nodiscard]] constexpr Approach approach_by(int32_t from, int32_t delta,
                                             int32_t backlash) noexcept {
    const int32_t target = from + delta;
    if (delta >= 0 || backlash <= 0) return {target, target};
    return {target - backlash, target};
}

[[nodiscard]] constexpr Approach approach(int32_t from, int32_t to, int32_t backlash) noexcept {
    return approach_by(from, shortest(from, to), backlash);
}

}  // namespace clk::domain
