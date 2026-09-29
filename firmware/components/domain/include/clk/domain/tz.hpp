// Time zones as POSIX TZ rules.                                 [FIRMWARE.md §6.4]
//
// "PST8PDT,M3.2.0,M11.1.0" is the whole of America/Los_Angeles for as long as the US keeps
// its 2007 rules: a standard offset, a daylight offset, and the two instants it switches.
// That is what a clock that sets itself from SNTP needs -- with no phone around, it still has
// to know that 02:00 on the second Sunday of March is 03:00.  The phone computes the string
// from its own zone database (app/PROTOCOL.md "Keeping time"), so there is no tz database here.
//
// Pure, allocation-free and independent of the C library's TZ (setenv + localtime is one
// process-wide global -- on the host that is every test at once).  Supported:
//   std offset [dst [offset] [,start[/time],end[/time]]]
//   names alphabetic (3+) or quoted <+0530>; offsets [+-]hh[:mm[:ss]], POSIX sign (west = +)
//   rules Mm.w.d, Jn (1..365, no Feb 29), n (0..365); times may be negative or past 24 h
// A dst name with no rule uses the US rule, as glibc does.
#pragma once

#include <cstddef>
#include <cstdint>

namespace clk::domain::tz {

inline constexpr const char* kDefault = "PST8PDT,M3.2.0,M11.1.0";  // San Francisco
inline constexpr const char* kDefaultName = "America/Los_Angeles";
inline constexpr std::size_t kPosixMax = 64;  // with the NUL; real zones are under 40

struct Rule {
    enum Kind : uint8_t { Month, Julian1, Julian0 } kind = Month;
    uint8_t m = 0, w = 0, d = 0;  // Mm.w.d
    uint16_t n = 0;               // Jn / n
    int32_t time_s = 7200;        // local wall time of the switch, default 02:00
};

struct Zone {
    int32_t std_off_s = 0;  // local = UTC + this (east positive -- the OPPOSITE of POSIX)
    int32_t dst_off_s = 0;
    bool has_dst = false;
    Rule start, end;
};

// ---- calendar ------------------------------------------------------------------------------

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's days_from_civil).
constexpr int64_t days_from_civil(int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

struct Ymd {
    int64_t y;
    unsigned m, d;
};

constexpr Ymd civil_from_days(int64_t z) noexcept {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return {static_cast<int64_t>(yoe) + era * 400 + (m <= 2), m, d};
}

constexpr int64_t floor_div(int64_t a, int64_t b) noexcept {
    return a / b - ((a % b != 0) && ((a < 0) != (b < 0)));
}

constexpr bool is_leap(int64_t y) noexcept { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

// 0 = Sunday.
constexpr unsigned weekday(int64_t days) noexcept {
    return static_cast<unsigned>(days >= -4 ? (days + 4) % 7 : (days + 5) % 7 + 6);
}

// The day (days since the epoch) a rule falls on in year `y`.
constexpr int64_t rule_day(Rule const& r, int64_t y) noexcept {
    const int64_t jan1 = days_from_civil(y, 1, 1);
    switch (r.kind) {
        case Rule::Julian1:  // 1..365, Feb 29 never counted
            return jan1 + r.n - 1 + (is_leap(y) && r.n >= 60 ? 1 : 0);
        case Rule::Julian0:
            return jan1 + r.n;
        case Rule::Month:
        default: {
            const int64_t first = days_from_civil(y, r.m, 1);
            int64_t day = first + (r.d + 7 - weekday(first)) % 7 + 7 * (r.w - 1);
            if (r.w == 5) {  // "the last": step back while past the month
                const unsigned next_m = r.m == 12 ? 1 : r.m + 1;
                const int64_t next_first = days_from_civil(r.m == 12 ? y + 1 : y, next_m, 1);
                while (day >= next_first) day -= 7;
            }
            return day;
        }
    }
}

// ---- evaluation ----------------------------------------------------------------------------

// Is daylight time in force at this UTC instant?
constexpr bool is_dst(Zone const& z, int64_t utc_s) noexcept {
    if (!z.has_dst) return false;
    const int64_t y = civil_from_days(floor_div(utc_s + z.std_off_s, 86400)).y;
    // The start is written in standard time and the end in daylight time (POSIX).
    const int64_t start = rule_day(z.start, y) * 86400 + z.start.time_s - z.std_off_s;
    const int64_t end = rule_day(z.end, y) * 86400 + z.end.time_s - z.dst_off_s;
    return start < end ? (utc_s >= start && utc_s < end)   // northern: summer inside the year
                       : (utc_s < end || utc_s >= start);  // southern: summer across New Year
}

// local = UTC + offset_s(utc).
constexpr int32_t offset_s(Zone const& z, int64_t utc_s) noexcept {
    return is_dst(z, utc_s) ? z.dst_off_s : z.std_off_s;
}

// A LOCAL wall time back to UTC.  A local time the spring-forward skips lands an hour later
// (02:30 -> 03:30); one the fall-back doubles resolves to the FIRST occurrence -- the same
// DST policy the alarm model states (§6.4).
constexpr int64_t local_to_utc(Zone const& z, int64_t local_s) noexcept {
    const int64_t as_std = local_s - z.std_off_s;
    const int64_t as_dst = local_s - z.dst_off_s;
    const bool s = offset_s(z, as_std) == z.std_off_s;  // consistent read as standard time?
    const bool d = z.has_dst && offset_s(z, as_dst) == z.dst_off_s;
    if (s && d) return as_dst < as_std ? as_dst : as_std;  // the doubled hour: the first one
    if (d) return as_dst;
    if (s) return as_std;
    return as_std;  // skipped: standard reading, which is an hour after the gap
}

// ---- parsing -------------------------------------------------------------------------------
namespace detail {

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool num(const char*& p, int lo, int hi, int& out) noexcept {
    if (!is_digit(*p)) return false;
    int v = 0;
    while (is_digit(*p)) {
        v = v * 10 + (*p++ - '0');
        if (v > hi) return false;
    }
    if (v < lo) return false;
    out = v;
    return true;
}

constexpr bool name(const char*& p) noexcept {
    if (*p == '<') {
        const char* q = ++p;
        while (*p && *p != '>') {
            if (!is_alpha(*p) && !is_digit(*p) && *p != '+' && *p != '-') return false;
            ++p;
        }
        if (*p != '>' || p - q < 3) return false;
        ++p;
        return true;
    }
    const char* q = p;
    while (is_alpha(*p)) ++p;
    return p - q >= 3;
}

// [+-]hh[:mm[:ss]] -> seconds, sign as written.
constexpr bool hms(const char*& p, int max_h, int32_t& out) noexcept {
    int sign = 1;
    if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
    int h = 0, m = 0, s = 0;
    if (!num(p, 0, max_h, h)) return false;
    if (*p == ':') {
        ++p;
        if (!num(p, 0, 59, m)) return false;
        if (*p == ':') {
            ++p;
            if (!num(p, 0, 59, s)) return false;
        }
    }
    out = sign * (h * 3600 + m * 60 + s);
    return true;
}

constexpr bool rule(const char*& p, Rule& r) noexcept {
    int a = 0, b = 0, c = 0;
    if (*p == 'M') {
        ++p;
        if (!num(p, 1, 12, a) || *p++ != '.' || !num(p, 1, 5, b) || *p++ != '.' || !num(p, 0, 6, c))
            return false;
        r.kind = Rule::Month;
        r.m = static_cast<uint8_t>(a);
        r.w = static_cast<uint8_t>(b);
        r.d = static_cast<uint8_t>(c);
    } else if (*p == 'J') {
        ++p;
        if (!num(p, 1, 365, a)) return false;
        r.kind = Rule::Julian1;
        r.n = static_cast<uint16_t>(a);
    } else {
        if (!num(p, 0, 365, a)) return false;
        r.kind = Rule::Julian0;
        r.n = static_cast<uint16_t>(a);
    }
    r.time_s = 7200;
    if (*p == '/') {
        ++p;
        if (!hms(p, 167, r.time_s)) return false;
    }
    return true;
}

}  // namespace detail

// False on anything it does not fully understand -- a zone that is half-parsed is a clock
// that is an hour out for half the year, which is worse than refusing.
constexpr bool parse(const char* s, Zone& out) noexcept {
    if (!s) return false;
    const char* p = s;
    Zone z{};
    int32_t off = 0;
    if (!detail::name(p) || !detail::hms(p, 24, off)) return false;
    z.std_off_s = -off;
    if (*p == '\0') {
        out = z;
        return true;
    }
    if (!detail::name(p)) return false;
    z.has_dst = true;
    z.dst_off_s = z.std_off_s + 3600;
    if (*p && *p != ',') {
        if (!detail::hms(p, 24, off)) return false;
        z.dst_off_s = -off;
    }
    if (*p == '\0') {  // no rule: the US one, as glibc assumes
        z.start = Rule{Rule::Month, 3, 2, 0, 0, 7200};
        z.end = Rule{Rule::Month, 11, 1, 0, 0, 7200};
    } else {
        if (*p++ != ',' || !detail::rule(p, z.start) || *p++ != ',' || !detail::rule(p, z.end) ||
            *p != '\0')
            return false;
    }
    out = z;
    return true;
}

// A fixed offset as a POSIX string: +120 min -> "<+0200>-2", -210 -> "<-0330>3:30".
inline void fixed(int off_min, char* out, std::size_t cap) noexcept {
    if (!out || cap == 0) return;
    const int a = off_min < 0 ? -off_min : off_min;
    char buf[24]{};
    std::size_t k = 0;
    auto put = [&](char c) {
        if (k + 1 < sizeof buf) buf[k++] = c;
    };
    auto two = [&](int v) {
        put(static_cast<char>('0' + v / 10));
        put(static_cast<char>('0' + v % 10));
    };
    auto dec = [&](int v) {
        if (v >= 10) put(static_cast<char>('0' + v / 10));
        put(static_cast<char>('0' + v % 10));
    };
    put('<');
    put(off_min < 0 ? '-' : '+');
    two(a / 60);
    two(a % 60);
    put('>');
    if (off_min > 0) put('-');  // POSIX: east of Greenwich is negative
    dec(a / 60);
    if (a % 60) {
        put(':');
        two(a % 60);
    }
    std::size_t i = 0;
    for (; i + 1 < cap && i < k; ++i) out[i] = buf[i];
    out[i] = '\0';
}

}  // namespace clk::domain::tz
