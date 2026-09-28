// The alarm-tone file format, checked -- not decoded.              [FIRMWARE.md D8, §6.3]
//
// ONE format, and it is the port's: 48 kHz, 16-bit signed little-endian PCM, MONO.  The amp is
// PBTL and plays one channel, I2S already runs at 48 kHz, and the ESP32-S3 is little-endian --
// so the samples after the header go into the ring exactly as they sit on the card.  No
// resampler, no mixdown, no decoder.
//
// Why a WAV at all rather than headerless .raw: the header is what lets a wrong file be REFUSED
// BY NAME.  A 44.1 kHz stereo file streamed as 48 kHz mono is not an error anybody sees -- it is
// a tone 9 % sharp at half speed, which on a bedside clock reads as "the speaker is broken".
//
// "Strict" means the fmt chunk must say exactly the above.  It does NOT mean the canonical
// 44-byte layout: Audacity and ffmpeg both write a LIST chunk ahead of `data`, and a check that
// refused those would refuse every file a person can actually make.  Unknown chunks are skipped.
//
//     ffmpeg -i in.mp3 -ac 1 -ar 48000 -c:a pcm_s16le -bitexact out.wav
#pragma once

#include <cstddef>
#include <cstdint>

namespace clk::hal::wav {

inline constexpr uint32_t kRateHz = 48000;
inline constexpr uint16_t kChannels = 1;
inline constexpr uint16_t kBits = 16;

// How much of the file parse() wants to see.  `data` has to start inside it; 512 bytes holds
// the RIFF header, fmt, and any LIST/INFO chunk a real encoder writes.
inline constexpr std::size_t kHeaderMax = 512;

enum class Err : uint8_t {
    Ok,
    NotWav,     // no RIFF/WAVE magic -- not a WAV at all
    NotPcm,     // format tag is not 1 (compressed, float, or EXTENSIBLE)
    Rate,       // not 48 000 Hz
    Channels,   // not mono
    Bits,       // not 16-bit
    NoData,     // no `data` chunk inside the first kHeaderMax bytes
    Truncated,  // a chunk header runs off the end of what was read
};

constexpr const char* name(Err e) noexcept {
    switch (e) {
        case Err::Ok:
            return "ok";
        case Err::NotWav:
            return "not a WAV (no RIFF/WAVE header)";
        case Err::NotPcm:
            return "not plain PCM (compressed or float)";
        case Err::Rate:
            return "sample rate is not 48000 Hz";
        case Err::Channels:
            return "not mono";
        case Err::Bits:
            return "not 16-bit";
        case Err::NoData:
            return "no data chunk in the first 512 bytes";
        case Err::Truncated:
            return "header truncated";
    }
    return "?";
}

// The same, as a token a program can switch on (the app's tone list, app/PROTOCOL.md).
// ⚠ On the wire: append, never rename.
constexpr const char* code(Err e) noexcept {
    switch (e) {
        case Err::Ok:
            return "ok";
        case Err::NotWav:
            return "not-wav";
        case Err::NotPcm:
            return "not-pcm";
        case Err::Rate:
            return "rate";
        case Err::Channels:
            return "channels";
        case Err::Bits:
            return "bits";
        case Err::NoData:
            return "no-data";
        case Err::Truncated:
            return "truncated";
    }
    return "?";
}

struct Info {
    Err err = Err::NotWav;
    // What the fmt chunk says, whether or not it passed -- so a refusal can say "44100 Hz
    // stereo" rather than only "wrong".
    uint32_t rate = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint16_t format = 0;
    uint32_t data_off = 0;    // byte offset of the first sample in the file
    uint32_t data_bytes = 0;  // clipped to what the file actually holds, and to whole samples

    [[nodiscard]] constexpr bool ok() const noexcept { return err == Err::Ok; }
    [[nodiscard]] constexpr uint32_t frames() const noexcept { return data_bytes / 2u; }
    // From the header's own rate and width, so a refused 44.1 kHz stereo file still reports
    // its real length.
    [[nodiscard]] constexpr uint32_t ms() const noexcept {
        const uint64_t bps = static_cast<uint64_t>(rate) * channels * (bits / 8u);
        return bps ? static_cast<uint32_t>(static_cast<uint64_t>(data_bytes) * 1000u / bps) : 0u;
    }
};

// `hdr` is the first `n` bytes of the file (up to kHeaderMax); `file_bytes` its full size, so a
// `data` chunk whose declared length runs past the end -- a copy that was cut short, or the
// 0xFFFFFFFF a streaming encoder writes -- is clipped to what is really there.
Info parse(const uint8_t* hdr, std::size_t n, uint32_t file_bytes) noexcept;

}  // namespace clk::hal::wav
