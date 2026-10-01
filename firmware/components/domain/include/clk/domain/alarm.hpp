// The weekly alarm, and the one-off that overrides it.        [FIRMWARE.md §6.6f, app/PROTOCOL.md]
//
// Pure -- no HAL, no clock, no state -- because the whole feature is a set of rules about
// dates, and dates are where alarms go wrong: the first day of a week, a midnight, a Monday
// evening turning into a Tuesday morning.  Rules like that are tested exhaustively on the host,
// not by waiting for 07:00.
//
// Two things, owned by two people:
//
//   * The WEEK -- a time per weekday, each day on or off.  The phone sets it (`chrono alarm
//     week`) and it is persisted.  The knob never touches it.
//   * The OVERRIDE -- "the next alarm is at 08:00", from the knob or `chrono alarm next`.  It
//     rings ONCE, at the next time the clock reads that minute, and it REPLACES the scheduled
//     alarm of the day it lands on.  Every other day is the week's again.  So: the week says
//     07:00 every day, it is Monday 22:00 and the knob says 08:00 -- Tuesday rings at 08:00
//     and not at 07:00, Wednesday at 07:00.
//
// Time here is the LOCAL minute: minutes since 1970-01-01 00:00 local (UTC + the zone's
// offset at that instant).  A day is 1440 of them, so a DST night is one local day like any
// other and "07:00" means 07:00 on the wall.
#pragma once

#include <cstdint>

namespace clk::domain::alarm {

inline constexpr int kDays = 7;  // ISO order: 0 = Monday .. 6 = Sunday
inline constexpr int kMinPerDay = 24 * 60;
inline constexpr int64_t kNone = INT64_MIN;
inline constexpr uint8_t kAllDays = 0x7F;
inline constexpr int16_t kDefaultMin = 7 * 60;

[[nodiscard]] constexpr int64_t floor_div(int64_t a, int64_t b) noexcept {
    const int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}
[[nodiscard]] constexpr int64_t local_minute(int64_t epoch_ms, int tz_off_min) noexcept {
    return floor_div(epoch_ms + tz_off_min * 60'000ll, 60'000);
}
[[nodiscard]] constexpr int64_t day_of(int64_t local_min) noexcept {
    return floor_div(local_min, kMinPerDay);
}
[[nodiscard]] constexpr int min_of_day(int64_t local_min) noexcept {
    return static_cast<int>(local_min - day_of(local_min) * kMinPerDay);
}
// 1970-01-01 was a Thursday: day 0 is weekday 3.
[[nodiscard]] constexpr int weekday(int64_t day) noexcept {
    return static_cast<int>(day + 3 - floor_div(day + 3, kDays) * kDays);
}

struct Week {
    uint8_t days = kAllDays;  // bit d set = weekday d rings
    // The time per weekday, KEPT while that day is off: switching Saturday back on brings its
    // 09:30 back rather than a default nobody chose.
    int16_t min[kDays] = {kDefaultMin, kDefaultMin, kDefaultMin, kDefaultMin,
                          kDefaultMin, kDefaultMin, kDefaultMin};

    [[nodiscard]] constexpr bool on(int wd) const noexcept { return (days >> wd) & 1u; }
    // Every day on, every day the same minute: a week that needs no weekday to be read.
    [[nodiscard]] constexpr bool uniform() const noexcept {
        if (days != kAllDays) return false;
        for (int d = 1; d < kDays; ++d)
            if (min[d] != min[0]) return false;
        return true;
    }
    [[nodiscard]] constexpr bool valid() const noexcept {
        if (days & ~kAllDays) return false;
        for (int16_t m : min)
            if (m < 0 || m >= kMinPerDay) return false;
        return true;
    }
    static constexpr Week daily(int16_t m) noexcept {
        Week w;
        for (auto& x : w.min) x = m;
        return w;
    }
    friend constexpr bool operator==(Week const&, Week const&) = default;
};

struct Override {
    int64_t at = kNone;   // the local minute it rings, once
    int64_t day = kNone;  // the local day whose scheduled alarm it replaces
    [[nodiscard]] constexpr bool pending() const noexcept { return at != kNone; }
    [[nodiscard]] constexpr bool empty() const noexcept { return at == kNone && day == kNone; }
    friend constexpr bool operator==(Override const&, Override const&) = default;
};

// On the wire (app/protocol.json `alarm_next`) -- append, never reorder.
enum class Src : uint8_t { None, Schedule, Override };

struct Next {
    int64_t at = kNone;  // local minute
    Src src = Src::None;
};

// Does the week ring on local day `d`, and at which minute?  With no real date (`dated`
// false: the clock was set by the knob and knows the hour, not the weekday) only a uniform
// week can be read -- every day is the same, so it does not matter which day this is.
[[nodiscard]] constexpr int scheduled_min(Week const& w, int64_t d, bool dated) noexcept {
    if (dated) return w.on(weekday(d)) ? w.min[weekday(d)] : -1;
    return w.uniform() ? w.min[0] : -1;
}

// The next alarm at or after `now`, override included, the replaced day excluded.
[[nodiscard]] constexpr Next next(Week const& w, Override const& o, int64_t now,
                                  bool dated) noexcept {
    Next n;
    if (o.pending() && o.at >= now) n = {o.at, Src::Override};
    const int64_t today = day_of(now);
    // Eight days, not seven: today's alarm may already be behind us, and next week's same day
    // is then the only one.
    for (int64_t d = today; d <= today + kDays; ++d) {
        if (d == o.day) continue;
        const int m = scheduled_min(w, d, dated);
        if (m < 0) continue;
        const int64_t t = d * kMinPerDay + m;
        if (t < now) continue;
        if (n.src == Src::None || t < n.at) n = {t, Src::Schedule};
        break;  // days only get later
    }
    return n;
}

// Does the alarm ring in minute `now`?
[[nodiscard]] constexpr bool due(Week const& w, Override const& o, int64_t now,
                                 bool dated) noexcept {
    return next(w, o, now, dated).at == now;
}

// "The next alarm is at `m`" -- the knob's edit.  Rings the next time the clock reads m (this
// minute included), replacing that day's scheduled alarm.  When the week already rings at
// exactly that minute on that day there is nothing to override: empty, and the week rules.
[[nodiscard]] constexpr Override override_at(Week const& w, int64_t now, int m,
                                             bool dated) noexcept {
    int64_t at = day_of(now) * kMinPerDay + m;
    if (at < now) at += kMinPerDay;
    const int64_t d = day_of(at);
    if (scheduled_min(w, d, dated) == m) return {};
    return {at, d};
}

// Drop what is behind us: an override that has rung (or whose minute the clock jumped past)
// and a replaced day that is over.
[[nodiscard]] constexpr Override expire(Override o, int64_t now) noexcept {
    if (o.at != kNone && o.at < now) o.at = kNone;
    if (o.day != kNone && o.day < day_of(now)) o.day = kNone;
    return o;
}

// "mon".."sun", for the console and the log.
[[nodiscard]] constexpr const char* day_name(int wd) noexcept {
    constexpr const char* k[kDays] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    return wd >= 0 && wd < kDays ? k[wd] : "?";
}

}  // namespace clk::domain::alarm
