// Light, over time.                                            [FIRMWARE.md §6.6a, led.md]
//
// Five patterns and two trivial ones, one envelope, one gamma.  Everything the product does
// with a pixel is one of these, which is the point: a breathing bell and a breathing battery
// warning must look like the same instrument, and they only do if they are the same code.
//
// Pure -- no HAL, no clock, no state.  You pass in `now`, you get a colour.  That is what
// makes a 30-minute sunrise ramp testable in a microsecond and what lets `ui` hold seven of
// these in an array and render them all from one tick.
//
//   shape(t)  0..255   the pattern's own curve, before anything is scaled
//   envelope  = shape * level / 255          <- what a test asserts on
//   render    = colour * gamma(envelope)     <- what the pixel gets
//
// Gamma is applied ONCE, here, at the end.  An SK6812's duty cycle is linear and the eye is
// not, so a linear ramp visibly stalls at the top; `level` is therefore a perceptual number
// ("60 % as bright as it goes"), not a duty cycle.
#pragma once

#include <cstdint>

#include "clk/hal/hal.hpp"

namespace clk::domain {

using Rgbw = hal::pixels::Rgbw;

// ---- the patterns ------------------------------------------------------------------------
enum class Pattern : uint8_t {
    Off,       // dark, and cheap to ask for
    Solid,     // hold at `level`
    RampUp,    // 0 -> level over `ms`, then hold          -- one-shot
    RampDown,  // level -> 0 over `ms`, then hold at 0     -- one-shot
    Swell,     // 0 -> level -> hold -> 0, then dark       -- one-shot; times from the config
    Breathe,   // 0 -> level -> 0, period `ms`; `repeats` of them, or forever at 0
    Blink,     // hard on/off square, period `ms`, duty from the config
    Flash,     // `repeats` quick flashes, then dark       -- one-shot when repeats > 0
};

[[nodiscard]] constexpr const char* name(Pattern p) noexcept {
    switch (p) {
        case Pattern::Off:
            return "off";
        case Pattern::Solid:
            return "solid";
        case Pattern::RampUp:
            return "ramp-up";
        case Pattern::RampDown:
            return "ramp-down";
        case Pattern::Swell:
            return "swell";
        case Pattern::Breathe:
            return "breathe";
        case Pattern::Blink:
            return "blink";
        case Pattern::Flash:
            return "flash";
    }
    return "?";
}

// ---- the config file ---------------------------------------------------------------------
// Every duration the light has, in one struct, so "consistent" is enforced by there being
// nowhere else to put a number.  `ui` owns one instance, `ui anim` edits it live, and §7.5
// persists it.  An Anim may override the duration per-instance (`ms`) -- that is how the
// same RampUp serves a 250 ms mode fade and a 30-minute sunrise.
struct AnimCfg {
    uint32_t ramp_ms = 250;       // the UI's own fade in / fade out
    uint32_t breathe_ms = 3200;   // one full dark -> lit -> dark cycle
    uint32_t blink_ms = 220;      // "fast blinking": one on+off period
    uint32_t flash_ms = 90;       // one flash of a burst, lit
    uint32_t flash_gap_ms = 110;  // and dark, between flashes
    // The Swell -- the one pattern with three durations, because it is the one pattern that
    // is a whole gesture rather than a state: it arrives, it stays long enough to be read,
    // and it leaves slower than it came so the room never sees it switch off.
    uint32_t swell_in_ms = 1000;
    uint32_t swell_hold_ms = 5000;
    uint32_t swell_out_ms = 4000;
    uint8_t blink_duty = 45;    // percent of blink_ms that is lit
    uint8_t breathe_floor = 0;  // 0..255: a breath that never goes fully dark
};

// ---- one animation -------------------------------------------------------------------------
// Trivially copyable and comparable: `ui` decides whether a cue CHANGED by comparing two of
// these, and only re-arms (resets t0) when it did.  That is what keeps five pixels asked to
// breathe at the same moment in phase forever, and what stops a 50 Hz repaint from pinning
// every animation to t=0.
struct Anim {
    Pattern pattern = Pattern::Off;
    Rgbw color{};
    uint8_t level = 255;  // "x": the destination intensity, perceptual
    uint8_t repeats = 0;  // Flash / Breathe: how many.  0 = forever (and never `done`)
    uint32_t ms = 0;      // duration/period override; 0 = take it from AnimCfg.  Swell: unused
                          //   -- all three of its durations live in the config
    uint64_t t0_us = 0;   // when this animation was armed
};

// t0 is deliberately NOT part of the comparison -- two cues asking for the same thing are
// the same cue no matter when they were armed.  See Ui::arm().
[[nodiscard]] constexpr bool same(Anim const& a, Anim const& b) noexcept {
    return a.pattern == b.pattern && a.color == b.color && a.level == b.level &&
           a.repeats == b.repeats && a.ms == b.ms;
}

// ---- curves --------------------------------------------------------------------------------
namespace detail {

// 3t^2 - 2t^3 on [0,1], in 0..255.  Within 1.7 % of the raised cosine (1-cos(pi*t))/2 and
// flat at both ends the same way, so a breath has no corner where it turns around -- and it
// is integer arithmetic, identical on the host and on the S3, which a libm cosf is not.
[[nodiscard]] constexpr uint8_t smooth_u8(uint32_t num, uint32_t den) noexcept {
    if (den == 0 || num >= den) return 255;
    const uint64_t t = static_cast<uint64_t>(num) * 1024u / den;                     // 0..1023
    const uint64_t s = (3u * t * t * 1024u - 2u * t * t * t) / (1024ull * 1024ull);  // 0..1024
    const uint64_t v = s * 255u / 1024u;
    return static_cast<uint8_t>(v > 255 ? 255 : v);
}

// Gamma 2.0.  Cheap, exact in integers, and the difference from 2.2 is under two counts --
// far less than the SK6812's own quantisation at the bottom of the range.
[[nodiscard]] constexpr uint8_t gamma_u8(uint8_t v) noexcept {
    return static_cast<uint8_t>((static_cast<uint32_t>(v) * v + 127u) / 255u);
}

[[nodiscard]] constexpr uint32_t elapsed_ms(Anim const& a, uint64_t now_us) noexcept {
    return now_us <= a.t0_us ? 0u : static_cast<uint32_t>((now_us - a.t0_us) / 1000ull);
}

[[nodiscard]] constexpr uint32_t period_of(Anim const& a, AnimCfg const& c) noexcept {
    if (a.ms) return a.ms;
    switch (a.pattern) {
        case Pattern::RampUp:
        case Pattern::RampDown:
            return c.ramp_ms;
        case Pattern::Breathe:
            return c.breathe_ms;
        case Pattern::Blink:
            return c.blink_ms;
        default:
            return 0;
    }
}

}  // namespace detail

// The pattern's own curve, 0..255, before `level`.
[[nodiscard]] constexpr uint8_t shape(Anim const& a, AnimCfg const& c, uint64_t now_us) noexcept {
    const uint32_t t = detail::elapsed_ms(a, now_us);
    const uint32_t p = detail::period_of(a, c);
    switch (a.pattern) {
        case Pattern::Off:
            return 0;
        case Pattern::Solid:
            return 255;
        case Pattern::RampUp:
            return t >= p ? 255 : detail::smooth_u8(t, p);
        case Pattern::RampDown:
            return t >= p ? 0 : static_cast<uint8_t>(255 - detail::smooth_u8(t, p));
        case Pattern::Swell: {
            // Rise, hold, fall, dark -- and the same eased curve on both ramps, so the way in
            // and the way out are recognisably one gesture at two speeds.
            if (t < c.swell_in_ms) return detail::smooth_u8(t, c.swell_in_ms);
            const uint32_t lit = c.swell_in_ms + c.swell_hold_ms;
            if (t < lit) return 255;
            if (t >= lit + c.swell_out_ms) return 0;
            return static_cast<uint8_t>(255 - detail::smooth_u8(t - lit, c.swell_out_ms));
        }
        case Pattern::Breathe: {
            if (p == 0) return 255;
            // A COUNTED breath ends dark on its own boundary rather than being cut off part
            // way up -- which is why the count is breaths and not milliseconds.
            if (a.repeats && t >= p * a.repeats) return 0;
            const uint32_t x = t % p;
            const uint32_t half = p / 2;
            const uint8_t s =
                x < half ? detail::smooth_u8(x, half)
                         : static_cast<uint8_t>(255 - detail::smooth_u8(x - half, p - half));
            // A floor keeps a slow breath legible in a lit room without ever reaching 0.
            return s < c.breathe_floor ? c.breathe_floor : s;
        }
        case Pattern::Blink: {
            if (p == 0) return 255;
            const uint32_t on = p * c.blink_duty / 100u;
            return (t % p) < on ? 255 : 0;
        }
        case Pattern::Flash: {
            const uint32_t one = c.flash_ms + c.flash_gap_ms;
            if (one == 0) return 0;
            if (a.repeats && t >= one * a.repeats) return 0;  // burst over
            return (t % one) < c.flash_ms ? 255 : 0;
        }
    }
    return 0;
}

// What the caller asked for, scaled by the curve.  Linear -- assert on this.
[[nodiscard]] constexpr uint8_t envelope(Anim const& a, AnimCfg const& c,
                                         uint64_t now_us) noexcept {
    return static_cast<uint8_t>(static_cast<uint32_t>(shape(a, c, now_us)) * a.level / 255u);
}

// Has a one-shot finished?  Periodic patterns never finish, and neither does an endless
// Flash -- `ui` uses this to know when a transient may give the pixel back to its mode.
[[nodiscard]] constexpr bool done(Anim const& a, AnimCfg const& c, uint64_t now_us) noexcept {
    const uint32_t t = detail::elapsed_ms(a, now_us);
    switch (a.pattern) {
        case Pattern::Off:
        case Pattern::Solid:
            return true;
        case Pattern::RampUp:
        case Pattern::RampDown:
            return t >= detail::period_of(a, c);
        case Pattern::Swell:
            return t >= c.swell_in_ms + c.swell_hold_ms + c.swell_out_ms;
        case Pattern::Breathe:
            return a.repeats && t >= detail::period_of(a, c) * a.repeats;
        case Pattern::Flash:
            return a.repeats && t >= (c.flash_ms + c.flash_gap_ms) * a.repeats;
        default:
            return false;
    }
}

// The pixel value.  Gamma lands here and nowhere else.
[[nodiscard]] constexpr Rgbw render(Anim const& a, AnimCfg const& c, uint64_t now_us) noexcept {
    const uint32_t e = detail::gamma_u8(envelope(a, c, now_us));
    if (e == 0) return Rgbw{};
    auto ch = [e](uint8_t v) { return static_cast<uint8_t>(static_cast<uint32_t>(v) * e / 255u); };
    return {ch(a.color.r), ch(a.color.g), ch(a.color.b), ch(a.color.w)};
}

// ---- the vocabulary the UX is written in ---------------------------------------------------
// Named so a cue table reads like the spec it implements (FIRMWARE.md §6.6b).
inline constexpr Rgbw kRed{255, 0, 0, 0};
inline constexpr Rgbw kWhite{0, 0, 0, 255};
inline constexpr Rgbw kBlue{0, 0, 255, 0};
inline constexpr Rgbw kAmber{255, 90, 0, 0};

[[nodiscard]] constexpr Anim off() noexcept { return {}; }
[[nodiscard]] constexpr Anim solid(Rgbw c, uint8_t level) noexcept {
    return {Pattern::Solid, c, level, 0, 0, 0};
}
[[nodiscard]] constexpr Anim ramp_up(Rgbw c, uint8_t level, uint32_t ms = 0) noexcept {
    return {Pattern::RampUp, c, level, 0, ms, 0};
}
[[nodiscard]] constexpr Anim ramp_down(Rgbw c, uint8_t level, uint32_t ms = 0) noexcept {
    return {Pattern::RampDown, c, level, 0, ms, 0};
}
// `times` 0 breathes forever (the alarm, the low cell, pairing); a count ends it dark.
[[nodiscard]] constexpr Anim breathe(Rgbw c, uint8_t level, uint32_t ms = 0,
                                     uint8_t times = 0) noexcept {
    return {Pattern::Breathe, c, level, times, ms, 0};
}
[[nodiscard]] constexpr Anim swell(Rgbw c, uint8_t level) noexcept {
    return {Pattern::Swell, c, level, 0, 0, 0};
}
[[nodiscard]] constexpr Anim blink(Rgbw c, uint8_t level, uint32_t ms = 0) noexcept {
    return {Pattern::Blink, c, level, 0, ms, 0};
}
[[nodiscard]] constexpr Anim flash(Rgbw c, uint8_t level, uint8_t times) noexcept {
    return {Pattern::Flash, c, level, times, 0, 0};
}

}  // namespace clk::domain
