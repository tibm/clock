// The WAV header check.  One copy, both backends.               [FIRMWARE.md D8]
#include "clk/hal/wav.hpp"

namespace clk::hal::wav {
namespace {

uint32_t le32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}
uint16_t le16(const uint8_t* p) noexcept { return static_cast<uint16_t>(p[0] | p[1] << 8); }
bool tag(const uint8_t* p, const char* t) noexcept {
    return p[0] == t[0] && p[1] == t[1] && p[2] == t[2] && p[3] == t[3];
}

}  // namespace

Info parse(const uint8_t* hdr, std::size_t n, uint32_t file_bytes) noexcept {
    Info in{};
    if (!hdr || n < 12 || !tag(hdr, "RIFF") || !tag(hdr + 8, "WAVE")) return in;

    bool have_fmt = false;
    std::size_t at = 12;
    for (;;) {
        if (at + 8 > n) {
            in.err = have_fmt ? Err::NoData : Err::Truncated;
            return in;
        }
        const uint8_t* ck = hdr + at;
        const uint32_t len = le32(ck + 4);
        if (tag(ck, "fmt ")) {
            if (len < 16 || at + 8 + 16 > n) {
                in.err = Err::Truncated;
                return in;
            }
            in.format = le16(ck + 8);
            in.channels = le16(ck + 10);
            in.rate = le32(ck + 12);
            in.bits = le16(ck + 22);
            have_fmt = true;
        } else if (tag(ck, "data")) {
            // fmt has to come first: a `data` before it would be samples in an unknown format.
            if (!have_fmt) {
                in.err = Err::Truncated;
                return in;
            }
            in.data_off = static_cast<uint32_t>(at + 8);
            const uint32_t avail = file_bytes > in.data_off ? file_bytes - in.data_off : 0u;
            in.data_bytes = (len < avail ? len : avail) & ~1u;  // whole 16-bit samples only
            break;
        }
        // Past the window either way -- and checked before the add, which on a 32-bit size_t
        // would wrap on a chunk that claims 4 GB and land back inside the buffer.
        if (len >= n) {
            in.err = have_fmt ? Err::NoData : Err::Truncated;
            return in;
        }
        // RIFF chunks are word-aligned: an odd length carries one pad byte.
        at += 8 + static_cast<std::size_t>(len) + (len & 1u);
    }

    // The order the refusals are checked in is the order that explains the most: a
    // compressed file's rate and channel fields are real but beside the point.
    if (in.format != 1) {
        in.err = Err::NotPcm;
    } else if (in.bits != kBits) {
        in.err = Err::Bits;
    } else if (in.rate != kRateHz) {
        in.err = Err::Rate;
    } else if (in.channels != kChannels) {
        in.err = Err::Channels;
    } else {
        in.err = Err::Ok;
    }
    return in;
}

}  // namespace clk::hal::wav
