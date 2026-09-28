// The stream source: a PSRAM ring of mono samples and the thing that drains it into I2S.
//                                                                   [FIRMWARE.md §6.2, §6.3]
//
// `storage` fills the ring from the card; the audio writer task drains it a 256-frame block at
// a time.  Two threads, one each side, so the ring is single-producer / single-consumer and
// needs nothing but two atomic indices.  2 s deep (§6.3) so that a 100 ms SD stall -- a FAT
// cluster-chain walk, a card doing its own wear levelling -- never reaches the speaker.
//
// The Mixer is where the rules about SOUND live, and it is in shared/ for the usual §11.2
// reason: the host tests assert on the very samples the ESP32 hands to DMA.
//
//   * Priming.  Nothing is played until the ring holds kPrimeMs, or the whole file.  Starting
//     on the first 8 KB would make the first SD hiccup an underrun.
//   * The ramp.  An alarm starts quiet and comes up over `ramp_ms` (§6.6: "tone ramps 30 s").
//     Digital gain, so the amp's volume register -- the user's setting -- is never touched.
//   * Underrun -> fade, never a click.  An empty ring does not step to zero: the last sample
//     decays over ~5 ms, and the stream fades back in when data returns.
//   * release() -> a 5 ms tail, then done.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace clk::hal::pcm {

// Single-producer single-consumer.  The storage is the caller's, so the target can put it in
// PSRAM and the host on the heap; the ring never allocates.
class Ring {
public:
    void attach(int16_t* buf, std::size_t cap) noexcept {
        buf_ = buf;
        cap_ = cap;
        clear();
    }
    // Only while neither side is running -- the audio HAL calls it with its writer idle.
    void clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return cap_; }
    [[nodiscard]] std::size_t level() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::size_t space() const noexcept { return cap_ - level(); }

    // Producer.  Returns how many samples went in.
    std::size_t write(const int16_t* src, std::size_t n) noexcept;
    // Consumer.  Returns how many samples came out.
    std::size_t read(int16_t* dst, std::size_t n) noexcept;

private:
    int16_t* buf_ = nullptr;
    std::size_t cap_ = 0;
    // Free-running counts, never wrapped to the capacity: level is head - tail in unsigned
    // arithmetic, which stays right across the 2^32 wrap and needs no "full vs empty" flag.
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

inline constexpr uint32_t kPrimeMs = 250;  // ring depth before the first sample plays
inline constexpr uint32_t kFadeMs = 5;     // release tail, underrun decay, recovery fade-in

class Mixer {
public:
    // `ramp_ms` 0 = full level at once.  Resets every counter.
    void start(uint32_t rate_hz, uint32_t ramp_ms) noexcept;
    void release() noexcept;  // fade out over kFadeMs, then done()

    // Fill `frames` interleaved stereo frames from `ring` (both slots the same sample, as
    // tone::Sine does).  `eof` = the producer has written its last sample; once the ring is
    // empty after that the stream is done rather than underrunning.  Always fills the whole
    // block (silence past the end), so the caller can hand it straight to DMA.
    void fill(Ring& ring, bool eof, int16_t* stereo, std::size_t frames) noexcept;

    [[nodiscard]] bool done() const noexcept { return done_; }
    [[nodiscard]] bool primed() const noexcept { return primed_; }
    [[nodiscard]] uint32_t underruns() const noexcept { return underruns_; }
    [[nodiscard]] uint64_t frames_played() const noexcept { return played_; }
    [[nodiscard]] float gain() const noexcept;  // the ramp's current value, 0..1

private:
    uint32_t rate_ = 0;
    uint32_t prime_ = 0;      // frames the ring must hold before the first one plays
    uint32_t fade_ = 0;       // frames in kFadeMs
    uint64_t ramp_ = 0;       // frames in the ramp; 0 = none
    uint64_t played_ = 0;     // ring frames consumed (the ramp's clock)
    uint32_t fade_in_ = 0;    // frames left of a fade-in after an underrun
    uint32_t tail_left_ = 0;  // frames left of release(); only meaningful while releasing
    bool releasing_ = false;
    bool primed_ = false;
    bool starved_ = false;  // inside an underrun right now -- counted once per gap
    bool done_ = true;
    float last_ = 0.0f;   // the last sample out, which an underrun decays from
    float decay_ = 0.0f;  // per-sample factor: -40 dB across kFadeMs
    uint32_t underruns_ = 0;
};

}  // namespace clk::hal::pcm
