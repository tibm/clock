// The ring and the mixer.  One copy, both backends.              [FIRMWARE.md §6.2, §6.3]
#include "clk/hal/pcm.hpp"

#include <cmath>
#include <cstring>

namespace clk::hal::pcm {

// ---- ring ------------------------------------------------------------------------------------

std::size_t Ring::write(const int16_t* src, std::size_t n) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t room = cap_ - (head - tail);
    if (n > room) n = room;
    const std::size_t at = head % cap_;
    const std::size_t first = n < cap_ - at ? n : cap_ - at;
    std::memcpy(buf_ + at, src, first * sizeof(int16_t));
    std::memcpy(buf_, src + first, (n - first) * sizeof(int16_t));
    head_.store(head + n, std::memory_order_release);
    return n;
}

std::size_t Ring::read(int16_t* dst, std::size_t n) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t have = head - tail;
    if (n > have) n = have;
    if (n == 0) return 0;
    const std::size_t at = tail % cap_;
    const std::size_t first = n < cap_ - at ? n : cap_ - at;
    std::memcpy(dst, buf_ + at, first * sizeof(int16_t));
    std::memcpy(dst + first, buf_, (n - first) * sizeof(int16_t));
    tail_.store(tail + n, std::memory_order_release);
    return n;
}

// ---- mixer -----------------------------------------------------------------------------------

namespace {

// Where the ramp starts: -30 dB, not silence.  Silence for the first few seconds of an alarm
// is indistinguishable from an alarm that did not go off, and on the bench from a dead amp.
constexpr float kRampFloor = 0.0316f;

int16_t to_i16(float x) noexcept {
    if (x >= 32767.0f) return 32767;
    if (x <= -32768.0f) return -32768;
    return static_cast<int16_t>(x);
}

}  // namespace

void Mixer::start(uint32_t rate_hz, uint32_t ramp_ms) noexcept {
    rate_ = rate_hz;
    prime_ = rate_hz * kPrimeMs / 1000u;
    fade_ = rate_hz * kFadeMs / 1000u;
    if (fade_ == 0) fade_ = 1;
    decay_ = std::pow(0.01f, 1.0f / static_cast<float>(fade_));
    ramp_ = static_cast<uint64_t>(rate_hz) * ramp_ms / 1000u;
    played_ = 0;
    fade_in_ = 0;
    tail_left_ = 0;
    releasing_ = false;
    primed_ = false;
    starved_ = false;
    done_ = rate_hz == 0;
    last_ = 0.0f;
    underruns_ = 0;
}

void Mixer::release() noexcept {
    if (done_ || releasing_) return;
    // A stream that never primed has made no sound; there is nothing to fade.
    if (!primed_) {
        done_ = true;
        return;
    }
    releasing_ = true;
    tail_left_ = fade_;
}

float Mixer::gain() const noexcept {
    if (ramp_ == 0 || played_ >= ramp_) return 1.0f;
    // Square law in amplitude: the ear hears level roughly logarithmically, and a linear
    // amplitude ramp spends its first half inaudible and its last few seconds jumping.
    const float t = static_cast<float>(played_) / static_cast<float>(ramp_);
    return kRampFloor + (1.0f - kRampFloor) * t * t;
}

void Mixer::fill(Ring& ring, bool eof, int16_t* stereo, std::size_t frames) noexcept {
    if (!stereo) return;
    if (!done_ && !primed_ && (ring.level() >= prime_ || eof)) primed_ = true;
    if (done_ || !primed_) {
        std::memset(stereo, 0, frames * 2 * sizeof(int16_t));
        return;
    }

    int16_t mono[256];
    std::size_t out = 0;
    while (out < frames) {
        const std::size_t want = frames - out < 256 ? frames - out : 256;
        const std::size_t got = done_ ? 0 : ring.read(mono, want);
        for (std::size_t k = 0; k < want; ++k) {
            float x = 0.0f;
            if (done_) {
                x = 0.0f;
            } else if (k < got) {
                x = static_cast<float>(mono[k]) * gain();
                ++played_;
                if (fade_in_) {
                    x *= 1.0f - static_cast<float>(fade_in_) / static_cast<float>(fade_);
                    --fade_in_;
                }
                starved_ = false;
            } else {
                // Nothing to play.  Decay from the last sample rather than stepping to zero --
                // a step into a Class-D bridge driving a 2" cone is a click.
                if (!eof && !starved_) {
                    ++underruns_;
                    starved_ = true;
                }
                if (!eof) fade_in_ = fade_;  // ... and come back in gently when data returns
                x = last_ * decay_;
            }
            if (releasing_ && !done_) {
                x *= static_cast<float>(tail_left_) / static_cast<float>(fade_);
                if (tail_left_ == 0 || --tail_left_ == 0) done_ = true;
            }
            last_ = x;
            const int16_t s = to_i16(x);
            stereo[2 * (out + k)] = s;
            stereo[2 * (out + k) + 1] = s;
        }
        out += want;
        // The end of the file: everything written has been played.  Past this the stream is
        // over, not starving -- the decay above has already taken the last sample down.
        if (eof && got < want && ring.level() == 0) done_ = true;
    }
}

}  // namespace clk::hal::pcm
