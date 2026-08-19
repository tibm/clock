// Which way is up.                                             [FIRMWARE.md §6.1d, §6.5.1]
//
// Gravity in, a tick out.  Pure -- no HAL, no clock, no state that anything else can see --
// because the interesting part of this feature is not reading the sensor, it is deciding
// WHEN a reading is worth moving both hands a hundred and eighty degrees for.  That decision
// is three rules (a dead zone, a Schmitt on it, and a confirmation count), and rules are
// worth testing exhaustively rather than by turning a cube over on a desk.
//
// Frame, everywhere in this file: the DIAL's own, the same one `hand.hpp` counts microsteps
// in.  +X is right across the face, +Y points at the printed 12, +Z comes out of the glass
// at you.  Mapping a BNO085 soldered at whatever angle onto that frame is the driver's job
// (§6.5.1), not this file's -- by the time gravity arrives here it is already in dial axes.
#pragma once

#include <cmath>
#include <cstdint>

#include "clk/domain/hand.hpp"

namespace clk::domain {

// Twelve dots on the dial, thirty degrees apart, and the offset is always one of them: any
// other angle would put the hands between the printed hours, which is what a clock that has
// been knocked looks like rather than one that has understood something.
inline constexpr int kTicks = 12;
inline constexpr int32_t kTickUsteps = kRev / kTicks;
static_assert(kTickUsteps * kTicks == kRev, "the dial must divide into twelve exactly");

// What to do when the dial faces the ceiling and "up" has no answer on it.  Zero today (the
// printed 12 is the 12, which is what a clock on its back has always done); Hold is the
// other half of the argument -- keep whatever the last upright reading said, so laying the
// clock down to change a battery does not spin the hands.  One line to switch, on purpose.
enum class FlatPolicy : uint8_t { Zero, Hold };

struct LevelCfg {
    // How much of gravity has to lie IN the dial plane before the answer means anything,
    // as a fraction of the whole vector: 1.0 is a dial standing straight up, 0.0 is one
    // lying flat.  Two numbers, not one -- a clock propped at exactly the threshold would
    // otherwise flip between a tick and no tick every time somebody walked past.
    float flat_in = 0.30f;   // below this, and we are flat        (~72 deg off vertical)
    float flat_out = 0.40f;  // ... and this much to be upright again (~66 deg)
    // How far past the halfway line between two ticks the reading must go before the tick
    // changes.  Without it, a cube sitting at exactly 15 degrees is a coin toss taken again
    // every poll, and a coin toss here costs half a turn of both hands.
    float hyst_deg = 6.0f;
    // ... and it must say so this many polls running.  A knock, a lift, a hand steadying the
    // cube: all of them are one sample long and none of them is a new orientation.
    uint8_t confirm = 2;
    FlatPolicy flat = FlatPolicy::Zero;
};

// Degrees into [0, 360).
[[nodiscard]] inline float wrap_deg(float d) noexcept {
    d = std::fmod(d, 360.0f);
    return d < 0.0f ? d + 360.0f : d;
}

// The signed way round from `from` to `to`, in (-180, +180].
[[nodiscard]] inline float delta_deg(float from, float to) noexcept {
    float d = wrap_deg(to) - wrap_deg(from);
    if (d > 180.0f) d -= 360.0f;
    if (d <= -180.0f) d += 360.0f;
    return d;
}

// Where "up" lies on the dial, degrees CLOCKWISE from the printed 12.  Up is -gravity, and
// the argument order is (x, y) rather than atan2's usual (y, x) because this frame measures
// clockwise from +Y rather than anticlockwise from +X.
[[nodiscard]] inline float up_deg(float gx, float gy) noexcept {
    return wrap_deg(std::atan2(-gx, -gy) * 180.0f / 3.14159265358979f);
}

[[nodiscard]] inline int nearest_tick(float deg) noexcept {
    const int t = static_cast<int>(std::floor(wrap_deg(deg) / 30.0f + 0.5f));
    return t % kTicks;
}

[[nodiscard]] inline float tick_deg(int tick) noexcept {
    return static_cast<float>(((tick % kTicks) + kTicks) % kTicks) * 30.0f;
}

// The offset that tick means, in microsteps: add it to every hand target and the printed 12
// stops being the 12.  Turn the cube 90 degrees clockwise and the dot that is now at the top
// is the printed 9, three ticks round -- so the hands are pushed round by the same three.
[[nodiscard]] inline int32_t tick_usteps(int tick) noexcept {
    return normalise(static_cast<int32_t>(((tick % kTicks) + kTicks) % kTicks) * kTickUsteps);
}

struct Level {
    int tick = 0;       // 0..11 -- the one the hands are actually using
    float up = 0.0f;    // where up was last seen, degrees clockwise from the printed 12
    float tilt = 0.0f;  // how much of gravity lies in the dial plane, 0..1
    bool flat = true;   // ... and whether that is too little to mean anything
};

// Feed it gravity, ask it what the dial should do.  Holds the small amount of state the
// three rules need and nothing else; one instance lives in `ui`, and every test of the rules
// above needs no active object, no sensor and no clock.
class Leveller {
public:
    LevelCfg cfg{};

    // One sample, in dial axes, any units (only the ratios matter).  True when the TICK
    // changed -- which is the only event worth a move, and is deliberately rare.
    bool update(float gx, float gy, float gz) noexcept {
        const float plane = std::sqrt(gx * gx + gy * gy);
        const float mag = std::sqrt(plane * plane + gz * gz);
        // A sensor answering zero, or NaN, or a number no gravity ever was, moves nothing.
        // (`!(mag > k)` rather than `mag <= k` so a NaN takes this branch rather than
        // sailing through the comparisons below and confirming a tick out of noise.)
        if (!(mag > 0.5f)) {
            seen_ = 0;
            cand_ = lv_.tick;
            return false;
        }
        lv_.tilt = plane / mag;
        lv_.up = up_deg(gx, gy);
        // Schmitt on the dead zone: it takes more tilt to stand up than to lie down.
        lv_.flat = lv_.flat ? (lv_.tilt < cfg.flat_out) : (lv_.tilt < cfg.flat_in);

        int want = lv_.tick;
        if (lv_.flat) {
            if (cfg.flat == FlatPolicy::Zero) want = 0;
        } else {
            want = nearest_tick(lv_.up);
            // Hysteresis: the halfway line is at 15 degrees, and leaving the tick we are on
            // costs a little more than reaching it did.
            if (want != lv_.tick &&
                std::fabs(delta_deg(tick_deg(lv_.tick), lv_.up)) < 15.0f + cfg.hyst_deg) {
                want = lv_.tick;
            }
        }

        if (want == lv_.tick) {
            seen_ = 0;
            cand_ = want;
            return false;
        }
        if (want != cand_) {
            cand_ = want;
            seen_ = 0;
        }
        if (++seen_ < (cfg.confirm ? cfg.confirm : 1)) return false;
        lv_.tick = want;
        seen_ = 0;
        return true;
    }

    [[nodiscard]] Level const& level() const noexcept { return lv_; }
    [[nodiscard]] int tick() const noexcept { return lv_.tick; }

    // Back to a cold boot: the printed 12 is the 12 until gravity says otherwise (§6.1d --
    // the tick is deliberately NOT stored, so there is nothing to disagree with reality).
    void reset() noexcept {
        lv_ = Level{};
        cand_ = 0;
        seen_ = 0;
    }

private:
    Level lv_{};
    int cand_ = 0;      // the tick being argued for
    uint8_t seen_ = 0;  // ... and how many polls running have agreed on it
};

}  // namespace clk::domain
