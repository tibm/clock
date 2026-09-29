// SNTP, the bytes.                                              [FIRMWARE.md §6.7, RFC 4330]
//
// One 48-byte request, one 48-byte answer.  The radio and the socket are hal::wifi's; the
// order the servers are tried in and when to try again are the `net` AO's.  What is here is
// the part that decides whether an answer is a TIME: the checks a client must make before it
// believes a server (§5 of the RFC), and the arithmetic from four timestamps to "UTC now".
//
// The clock has no trustworthy UTC of its own when it asks -- that is why it is asking -- so
// the request's transmit timestamp is a NONCE, not a time.  A server copies it into the
// answer's originate field, which is how a stale or spoofed datagram is told apart from the
// answer to this request.  Round-trip time comes from the monotonic clock instead.
#pragma once

#include <cstddef>
#include <cstdint>

namespace clk::domain::sntp {

inline constexpr std::size_t kPacket = 48;
inline constexpr uint16_t kPort = 123;
// NTP counts seconds from 1900; Unix from 1970.
inline constexpr int64_t kNtpToUnix = 2'208'988'800LL;

enum class Verdict : uint8_t {
    Ok,
    Short,        // fewer than 48 bytes
    NotServer,    // mode is not 4 (server), or a version we do not speak
    KissOfDeath,  // stratum 0: the server said go away (RATE, DENY, ...) -- try another
    Unsynced,     // leap indicator 3, or stratum > 15: the server does not know the time
    BadOrigin,    // not the answer to our request
    NoTime,       // transmit timestamp zero
    TooEarly,     // a date before this firmware could have been built
};

constexpr const char* name(Verdict v) noexcept {
    switch (v) {
        case Verdict::Ok:
            return "ok";
        case Verdict::Short:
            return "short";
        case Verdict::NotServer:
            return "not a server reply";
        case Verdict::KissOfDeath:
            return "kiss-of-death";
        case Verdict::Unsynced:
            return "server unsynchronised";
        case Verdict::BadOrigin:
            return "origin mismatch";
        case Verdict::NoTime:
            return "no transmit time";
        case Verdict::TooEarly:
            return "implausible date";
    }
    return "?";
}

// No answer that dates the clock before this is believed.  2026-01-01T00:00:00Z.
inline constexpr int64_t kMinUnixMs = 1'767'225'600'000LL;

namespace detail {
constexpr uint64_t be64(const uint8_t* p) noexcept {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}
// 32.32 fixed-point NTP time -> Unix ms.  Era 0 ends 2036-02-07; a seconds field with the top
// bit clear is taken as era 1 (RFC 4330 §3), so this keeps working past 2036.
constexpr int64_t ntp_to_unix_ms(uint64_t ts) noexcept {
    int64_t s = static_cast<int64_t>(ts >> 32);
    if (s < 0x8000'0000LL) s += 0x1'0000'0000LL;
    const int64_t frac_ms =
        static_cast<int64_t>(((ts & 0xFFFF'FFFFull) * 1000ull + 0x8000'0000ull) >> 32);
    return (s - kNtpToUnix) * 1000 + frac_ms;
}
}  // namespace detail

// A client request.  LI 0, version 4, mode 3; everything zero but the nonce.
constexpr void build_request(uint8_t (&out)[kPacket], uint64_t nonce) noexcept {
    for (auto& b : out) b = 0;
    out[0] = (0 << 6) | (4 << 3) | 3;
    for (int i = 0; i < 8; ++i) out[40 + i] = static_cast<uint8_t>(nonce >> (56 - 8 * i));
}

struct Reply {
    uint8_t stratum;
    int64_t rx_ms;  // T2: when the server received the request (Unix ms)
    int64_t tx_ms;  // T3: when it sent the answer
    char kod[5];    // the kiss code, when stratum is 0
};

constexpr Verdict parse(const uint8_t* p, std::size_t n, uint64_t nonce, Reply& r) noexcept {
    if (!p || n < kPacket) return Verdict::Short;
    const unsigned li = p[0] >> 6, vn = (p[0] >> 3) & 7, mode = p[0] & 7;
    if (mode != 4 || vn < 3 || vn > 4) return Verdict::NotServer;
    r.stratum = p[1];
    for (int i = 0; i < 4; ++i) r.kod[i] = static_cast<char>(p[12 + i]);
    r.kod[4] = '\0';
    if (detail::be64(p + 24) != nonce) return Verdict::BadOrigin;
    if (r.stratum == 0) return Verdict::KissOfDeath;
    if (li == 3 || r.stratum > 15) return Verdict::Unsynced;
    const uint64_t tx = detail::be64(p + 40);
    if (tx == 0) return Verdict::NoTime;
    r.rx_ms = detail::ntp_to_unix_ms(detail::be64(p + 32));
    r.tx_ms = detail::ntp_to_unix_ms(tx);
    if (r.tx_ms < kMinUnixMs) return Verdict::TooEarly;
    return Verdict::Ok;
}

// UTC at the moment the answer ARRIVED: the server's send time plus half the network part
// of the round trip (total, from the monotonic clock, minus the server's own hold time).
constexpr int64_t utc_at_arrival_ms(Reply const& r, uint64_t sent_us, uint64_t recv_us) noexcept {
    int64_t rtt = static_cast<int64_t>((recv_us - sent_us) / 1000) - (r.tx_ms - r.rx_ms);
    if (rtt < 0) rtt = 0;
    return r.tx_ms + rtt / 2;
}

}  // namespace clk::domain::sntp
