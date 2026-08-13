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

// Where to actually drive, given the backlash policy: every move FINISHES clockwise (§6.1),
// so an anticlockwise move undershoots by `backlash` and comes back up through the slop.
// Returns the two legs; leg 2 == target when no doubling back is needed.
struct Approach {
    int32_t via;     // drive here first (== target if the move is already clockwise)
    int32_t target;  // and finish here, always arriving clockwise
};

[[nodiscard]] constexpr Approach approach(int32_t from, int32_t to, int32_t backlash) noexcept {
    const int32_t d = shortest(from, to);
    const int32_t target = from + d;
    if (d >= 0 || backlash <= 0) return {target, target};
    return {target - backlash, target};
}

}  // namespace clk::domain
