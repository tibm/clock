// A sine, generated a block at a time.                     [FIRMWARE.md §6.2, §12.0.15]
//
// This is the bring-up signal and the preview chime -- the only audio source that exists
// before `storage` (§6.3) can hand `audio` (§6.2) a WAV ring.  It is deliberately NOT a
// stub: the envelope, the phase accumulator and the stereo interleave are the same three
// things a real source has to get right, and getting them wrong sounds like a hardware
// fault (clicks at every block boundary, a beat against the sample rate).
//
// Lives in shared/ rather than in either backend for the usual §11.2 reason: a host test
// asserting on the samples is only worth anything if they are literally the samples the
// ESP32 pushes into I2S.
#pragma once

#include <cstddef>
#include <cstdint>

namespace clk::hal::tone {

// A short raised-cosine at each end.  Without it a beep starts and stops on a step, and a
// step into a Class-D amp driving a 2" cone is an audible tick -- which on a bring-up bench
// is indistinguishable from a bad solder joint.
inline constexpr uint32_t kFadeMs = 5;

inline constexpr uint32_t kMinHz = 20;
inline constexpr uint32_t kMaxHz = 20000;

class Sine {
public:
    // `amp` is peak amplitude as a fraction of full scale (0..1); `ms` == 0 plays until
    // release().  Restarting resets the phase, which is what you want for a beep and is why
    // the phase is not preserved across start()s.
    void start(uint32_t hz, uint32_t rate_hz, float amp, uint32_t ms) noexcept;

    // Begin the tail fade from wherever the tone currently is.  Idempotent, and a no-op on a
    // tone that is already fading out or finished.
    void release() noexcept;

    // Fills `frames` INTERLEAVED STEREO frames and returns how many of them the generator
    // produced; anything past that is zero-filled, so the caller can always hand the whole
    // block to DMA.  Both slots carry the same sample: the amp is PBTL and reads one of them
    // (`PBTL Ch Sel`, reg 0x06 bit 1), but which one is a strap-level detail and a silent
    // slot would be a bug that only shows up on hardware.
    std::size_t fill(int16_t* stereo, std::size_t frames) noexcept;

    [[nodiscard]] bool done() const noexcept { return done_; }
    [[nodiscard]] uint32_t hz() const noexcept { return hz_; }
    [[nodiscard]] uint32_t frames_emitted() const noexcept { return pos_; }

private:
    [[nodiscard]] float envelope(uint32_t pos) const noexcept;

    uint32_t hz_ = 0;
    uint32_t rate_ = 0;
    uint32_t phase_ = 0;  // Q32 turns -- wraps exactly, so a long tone cannot drift
    uint32_t step_ = 0;
    uint32_t pos_ = 0;    // frames emitted since start()
    uint32_t total_ = 0;  // frames to emit in all; 0 = until release()
    uint32_t fade_ = 0;   // frames in each of the two ramps
    float amp_ = 0.0f;
    bool done_ = true;
};

}  // namespace clk::hal::tone
