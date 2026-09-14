// The sine generator.  One copy, both backends.                        [FIRMWARE.md §6.2]
#include "clk/hal/tone.hpp"

#include <cmath>

namespace clk::hal::tone {
namespace {

constexpr float kTwoPiOverQ32 = 6.283185307179586f / 4294967296.0f;

uint32_t clampu(uint32_t v, uint32_t lo, uint32_t hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

int16_t to_i16(float x) noexcept {
    const float y = x * 32767.0f;
    if (y >= 32767.0f) return 32767;
    if (y <= -32768.0f) return -32768;
    return static_cast<int16_t>(y);
}

}  // namespace

void Sine::start(uint32_t hz, uint32_t rate_hz, float amp, uint32_t ms) noexcept {
    if (rate_hz == 0) {
        done_ = true;
        return;
    }
    hz_ = clampu(hz, kMinHz, kMaxHz);
    rate_ = rate_hz;
    amp_ = amp < 0.0f ? 0.0f : (amp > 1.0f ? 1.0f : amp);
    phase_ = 0;
    step_ = static_cast<uint32_t>((static_cast<uint64_t>(hz_) << 32) / rate_);
    pos_ = 0;
    total_ = static_cast<uint32_t>((static_cast<uint64_t>(ms) * rate_) / 1000u);
    fade_ = (kFadeMs * rate_) / 1000u;
    // A beep shorter than two fades is all envelope.  Halving rather than refusing keeps a
    // 3 ms tick expressible -- it just comes out as a triangle, which is the correct shape
    // for something that short.
    if (total_ != 0 && fade_ > total_ / 2) fade_ = total_ / 2;
    // A duration that rounds to nothing (`audio tone 440 0ms`) is finished before it starts.
    // Saying so here is what keeps fill() from emitting one lone sample.
    done_ = (ms != 0 && total_ == 0);
}

void Sine::release() noexcept {
    if (done_) return;
    // Already inside the tail: leave it alone rather than restarting the ramp, which would
    // step the envelope back up and click.
    if (total_ != 0 && pos_ + fade_ >= total_) return;
    total_ = pos_ + fade_;
    if (total_ == 0) done_ = true;
}

float Sine::envelope(uint32_t pos) const noexcept {
    if (fade_ == 0) return 1.0f;
    if (pos < fade_) {
        const float t = static_cast<float>(pos) / static_cast<float>(fade_);
        return 0.5f * (1.0f - std::cos(3.14159265f * t));
    }
    if (total_ != 0 && pos + fade_ >= total_) {
        const uint32_t left = total_ > pos ? total_ - pos : 0u;
        const float t = static_cast<float>(left) / static_cast<float>(fade_);
        return 0.5f * (1.0f - std::cos(3.14159265f * t));
    }
    return 1.0f;
}

std::size_t Sine::fill(int16_t* stereo, std::size_t frames) noexcept {
    if (!stereo) return 0;
    std::size_t made = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        int16_t s = 0;
        if (!done_) {
            const float v =
                amp_ * envelope(pos_) * std::sin(static_cast<float>(phase_) * kTwoPiOverQ32);
            s = to_i16(v);
            phase_ += step_;
            ++pos_;
            if (total_ != 0 && pos_ >= total_) done_ = true;
            ++made;
        }
        stereo[2 * i] = s;
        stereo[2 * i + 1] = s;
    }
    return made;
}

}  // namespace clk::hal::tone
