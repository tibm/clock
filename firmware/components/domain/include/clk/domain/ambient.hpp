// The room's light, turned into how bright the clock's own light may be.   [FIRMWARE.md §6.6g]
//
// Lux in, a scale out: 255 = `Tuning::brightness` as set, less = dimmer.  It only ever REDUCES
// (§6.6b) -- a clock that got brighter than you set it because the sun came out would be a
// clock arguing with you.  Pure: no HAL, no clock; `now` is passed in.
//
// Three rules, and they are the feature:
//
//   1. A LOG map.  The eye answers to ratios, and a bedside clock sees six decades between
//      noon and 3 a.m.  `night_lux` and below is the night floor, `day_lux` and above is full,
//      straight line in log(lux) between.
//   2. A Schmitt on the room, in decades.  The TSL2591 reading wobbles by a few percent and the
//      room's light flickers; neither is a reason to re-level the pixels.  The target only moves
//      when the room has moved `hyst_dec` decades from where it last moved it.
//   3. A slew.  A lamp switched off is a step in lux and must not be a step in the pixels --
//      they glide to the new level over `slew_ms` (end to end), so nobody sees it switch.
//
// No opinion = full brightness: no sensor, a reading older than `stale_ms`, or the feature off.
// Saturated reads as day: the channels only clip in daylight.
#pragma once

#include <cmath>
#include <cstdint>

namespace clk::domain {

struct AmbientCfg {
    bool on = true;
    float night_lux = 1.0f;   // at or below: the night floor
    float day_lux = 50.0f;    // at or above: full -- a lamp-lit room is already "day" for a pixel
    uint8_t night_pct = 20;   // the floor, percent of the level (perceptual, before gamma)
    float hyst_dec = 0.15f;   // decades (~x1.4) the room must move before the target follows
    uint32_t slew_ms = 2000;  // 0 -> 100 % takes this long; any step proportionally less
    uint32_t stale_ms = 30000;
};

// One TSL2591 reading as `ui` gets it (from `net`'s status record until `board` exists, §6.5).
struct Lux {
    bool ok = false;  // a reading exists
    float lux = 0.0f;
    bool saturated = false;
    uint32_t age_ms = 0;
};

// The map alone -- rule 1.  0..255.
[[nodiscard]] inline uint8_t room_scale(float lux, AmbientCfg const& c) noexcept {
    const uint32_t floor = 255u * (c.night_pct > 100 ? 100u : c.night_pct) / 100u;
    if (!(c.day_lux > c.night_lux) || c.night_lux <= 0.0f)
        return 255;  // nonsense config: no dimming
    if (lux <= c.night_lux) return static_cast<uint8_t>(floor);
    if (lux >= c.day_lux) return 255;
    const float t = std::log10(lux / c.night_lux) / std::log10(c.day_lux / c.night_lux);
    return static_cast<uint8_t>(static_cast<float>(floor) + t * static_cast<float>(255u - floor) +
                                0.5f);
}

class Dimmer {
public:
    // Feed it every tick; it returns the scale to use NOW.
    uint8_t update(Lux const& r, uint64_t now_us, AmbientCfg const& c) noexcept {
        const bool known = c.on && r.ok && r.age_ms <= c.stale_ms;
        if (!known) {
            anchor_ = -1.0f;  // the next real reading takes the target straight away
            target_ = 255;
        } else {
            // Below a hundredth of a lux the log is noise; a dark room is a dark room.
            const float lux =
                r.saturated || r.lux < 0.0f ? c.day_lux : (r.lux < 0.01f ? 0.01f : r.lux);
            if (anchor_ <= 0.0f || std::fabs(std::log10(lux / anchor_)) >= c.hyst_dec) {
                anchor_ = lux;
                target_ = room_scale(lux, c);
            }
        }
        // Rule 3.
        if (!started_) {
            started_ = true;
            cur_ = target_;
        } else if (c.slew_ms == 0) {
            cur_ = target_;
        } else {
            const float step = 255.0f * static_cast<float>(now_us - at_us_) / 1000.0f /
                               static_cast<float>(c.slew_ms);
            const float want = target_;
            cur_ = cur_ < want ? (cur_ + step > want ? want : cur_ + step)
                               : (cur_ - step < want ? want : cur_ - step);
        }
        at_us_ = now_us;
        return scale();
    }

    [[nodiscard]] uint8_t scale() const noexcept { return static_cast<uint8_t>(cur_ + 0.5f); }
    [[nodiscard]] uint8_t target() const noexcept { return target_; }
    [[nodiscard]] float anchor_lux() const noexcept { return anchor_; }  // -1 = no opinion

private:
    float anchor_ = -1.0f;  // the lux the target was last moved at
    uint8_t target_ = 255;
    float cur_ = 255.0f;
    uint64_t at_us_ = 0;
    bool started_ = false;
};

// A level scaled by the room.  Never rounds a lit pixel to nothing: a scale that would put a
// level of 1 or more at 0 gives it 1 -- the floor is a floor, not a fourth way to be dark.
[[nodiscard]] constexpr uint8_t dim(uint8_t level, uint8_t scale) noexcept {
    if (level == 0 || scale == 0) return 0;
    const uint32_t v = static_cast<uint32_t>(level) * scale / 255u;
    return static_cast<uint8_t>(v ? v : 1u);
}

}  // namespace clk::domain
