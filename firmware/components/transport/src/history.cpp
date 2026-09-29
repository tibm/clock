#include "clk/transport/history.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace clk::transport::hist {
namespace {

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
    put16(p, static_cast<uint16_t>(v));
    put16(p + 2, static_cast<uint16_t>(v >> 16));
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

long round_clamp(double v, long lo, long hi) {
    if (!(v == v)) return lo;
    const long r = std::lround(v);
    return r < lo ? lo : (r > hi ? hi : r);
}

// Howard Hinnant's civil <-> days, as in domain/tz.hpp (transport may not depend on domain).
int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}
void civil(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2);
}

}  // namespace

const char* name(Ev e) noexcept {
    switch (e) {
        case Ev::Boot:
            return "boot";
        case Ev::AlarmFire:
            return "alarm-fire";
        case Ev::AlarmSnooze:
            return "alarm-snooze";
        case Ev::AlarmDismiss:
            return "alarm-dismiss";
        case Ev::WifiOnline:
            return "wifi-online";
        case Ev::WifiFail:
            return "wifi-fail";
        case Ev::SntpSync:
            return "sntp-sync";
        case Ev::TimeSet:
            return "time-set";
        case Ev::LogConfig:
            return "log-config";
    }
    return "?";
}

uint16_t enc_gas(uint32_t ohms) noexcept {
    if (ohms <= 1) return 0;
    return static_cast<uint16_t>(
        round_clamp(std::log10(static_cast<double>(ohms)) * 8192.0, 0, 65535));
}
uint32_t dec_gas(uint16_t v) noexcept {
    return static_cast<uint32_t>(std::lround(std::pow(10.0, v / 8192.0)));
}
uint16_t enc_lux(float lux) noexcept {
    if (!(lux > 0.0f)) return 0;
    return static_cast<uint16_t>(round_clamp(std::log10(lux + 1.0) * 12000.0, 0, 65535));
}
float dec_lux(uint16_t v) noexcept { return static_cast<float>(std::pow(10.0, v / 12000.0) - 1.0); }
uint8_t enc_lux8(float lux) noexcept {
    if (!(lux > 0.0f)) return 0;
    return static_cast<uint8_t>(round_clamp(std::log10(lux + 1.0) * 50.0, 0, 255));
}
float dec_lux8(uint8_t v) noexcept { return static_cast<float>(std::pow(10.0, v / 50.0) - 1.0); }

uint8_t crc8(const uint8_t* p, std::size_t n) noexcept {
    uint8_t c = 0;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b) c = static_cast<uint8_t>(c & 0x80 ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

void encode(Sample const& s, uint8_t (&o)[kRecord]) noexcept {
    std::memset(o, 0, sizeof o);
    o[0] = kSample;
    o[1] = s.n;
    put32(o + 2, s.t);
    put16(o + 6, static_cast<uint16_t>(s.temp_cdeg));
    put16(o + 8, s.rh_cpct);
    put16(o + 10, s.press_dhpa);
    put16(o + 12, enc_gas(s.gas_ohms));
    put16(o + 14, enc_lux(s.lux));
    put16(o + 16, s.vbat_mv);
    o[18] = s.soc_pct;
    o[19] = static_cast<uint8_t>(s.rssi);
    put16(o + 20, s.flags);
    o[22] = enc_lux8(s.lux_max);
    o[23] = crc8(o, kRecord - 1);
}

void encode(Event const& e, uint8_t (&o)[kRecord]) noexcept {
    std::memset(o, 0, sizeof o);
    o[0] = kEvent;
    o[1] = static_cast<uint8_t>(e.code);
    put32(o + 2, e.t);
    std::memcpy(o + 6, e.args, sizeof e.args);
    o[23] = crc8(o, kRecord - 1);
}

bool decode(const uint8_t* in, Record& r) noexcept {
    if (!in || crc8(in, kRecord - 1) != in[kRecord - 1]) return false;
    r = Record{};
    r.kind = static_cast<Kind>(in[0]);
    if (in[0] == kSample) {
        Sample& s = r.s;
        s.n = in[1];
        s.t = get32(in + 2);
        s.temp_cdeg = static_cast<int16_t>(get16(in + 6));
        s.rh_cpct = get16(in + 8);
        s.press_dhpa = get16(in + 10);
        s.gas_ohms = dec_gas(get16(in + 12));
        s.lux = dec_lux(get16(in + 14));
        s.vbat_mv = get16(in + 16);
        s.soc_pct = in[18];
        s.rssi = static_cast<int8_t>(in[19]);
        s.flags = get16(in + 20);
        s.lux_max = dec_lux8(in[22]);
        return true;
    }
    if (in[0] == kEvent) {
        r.e.code = static_cast<Ev>(in[1]);
        r.e.t = get32(in + 2);
        std::memcpy(r.e.args, in + 6, sizeof r.e.args);
        return true;
    }
    return false;
}

void encode(Header const& h, uint8_t (&o)[kHeader]) noexcept {
    std::memset(o, 0, sizeof o);
    std::memcpy(o, kMagic, 4);
    o[4] = kVersion;
    o[5] = kRecord;
    put16(o + 6, h.period_s);
    put32(o + 8, h.day);
    put32(o + 12, h.fw_id);
}

bool decode_header(const uint8_t* in, std::size_t n, Header& h) noexcept {
    if (!in || n < kHeader || std::memcmp(in, kMagic, 4) != 0 || in[4] != kVersion ||
        in[5] != kRecord)
        return false;
    h.period_s = get16(in + 6);
    h.day = get32(in + 8);
    h.fw_id = get32(in + 12);
    return true;
}

uint32_t day_start(uint32_t t) noexcept { return t - t % 86400u; }

uint32_t yyyymmdd(uint32_t t) noexcept {
    int64_t y;
    unsigned m, d;
    civil(t / 86400u, y, m, d);
    return static_cast<uint32_t>(y * 10000 + m * 100 + d);
}

uint32_t from_yyyymmdd(uint32_t v) noexcept {
    const unsigned d = v % 100, m = (v / 100) % 100;
    const int64_t y = v / 10000;
    if (y < 1970 || y > 2105 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    const int64_t days = days_from_civil(y, m, d);
    int64_t yy;
    unsigned mm, dd;
    civil(days, yy, mm, dd);
    if (mm != m || dd != d) return 0;  // 20260231
    return static_cast<uint32_t>(days * 86400);
}

bool day_path(uint32_t t, char* out, std::size_t cap) noexcept {
    const uint32_t v = yyyymmdd(t);
    const int n = std::snprintf(out, cap, "/sd/log/%04u/%04u.bin", static_cast<unsigned>(v / 10000),
                                static_cast<unsigned>(v % 10000));
    return n > 0 && static_cast<std::size_t>(n) < cap;
}

uint64_t day_bytes(uint32_t period_s, uint32_t cluster) noexcept {
    if (period_s == 0) return 0;
    const uint64_t raw =
        kHeader + (86400u + period_s - 1) / period_s * kRecord + uint64_t{kEventsPerDay} * kRecord;
    if (cluster == 0) return raw;
    return (raw + cluster - 1) / cluster * cluster;
}

uint64_t projected_bytes(uint32_t period_s, uint32_t keep_days, uint32_t cluster) noexcept {
    return day_bytes(period_s, cluster) * keep_days;
}

}  // namespace clk::transport::hist
