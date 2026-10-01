// The weekly alarm and its one-off override.                  [FIRMWARE.md §6.6f, §11.1]
//
// All of it is domain/alarm.hpp, which is pure -- so a case here is a week lived in a
// microsecond, not a clock left running until Tuesday.  The AO half (the knob, NVS, the wire)
// is in test_storage.cpp and test_net.cpp.
#include "check.hpp"

#include "clk/domain/alarm.hpp"

using namespace clk::domain::alarm;

namespace {

constexpr int64_t kMon = 20724;  // 2026-09-28, a Monday, as a local day number
constexpr int64_t at(int64_t day, int h, int m) { return day * kMinPerDay + h * 60 + m; }

void test_alarm_calendar_arithmetic() {
    CHECK(weekday(0) == 3);  // 1970-01-01, a Thursday
    CHECK(weekday(kMon) == 0);
    CHECK(weekday(kMon + 6) == 6);
    CHECK(weekday(-1) == 2);  // 1969-12-31, a Wednesday: the floor, not the truncation
    CHECK(day_of(-1) == -1 && min_of_day(-1) == kMinPerDay - 1);
    // 2026-09-28 07:00 UTC+2 is 05:00 UTC.
    const int64_t utc_ms = (kMon * 86'400LL + 5 * 3600) * 1000;
    CHECK(local_minute(utc_ms, 120) == at(kMon, 7, 0));
    CHECK(local_minute(utc_ms + 59'999, 120) == at(kMon, 7, 0));
}

void test_alarm_week_skips_days_off() {
    Week w = Week::daily(7 * 60);
    w.days = 0x1F;           // weekdays
    w.min[5] = 9 * 60 + 30;  // Saturday's time, kept while off
    CHECK(!w.uniform() && w.valid());
    // Friday 08:00, after the alarm: the next one is Monday, not Saturday.
    const auto n = next(w, {}, at(kMon + 4, 8, 0), true);
    CHECK(n.src == Src::Schedule && n.at == at(kMon + 7, 7, 0));
    // Saturday back on: its own 09:30 is back.
    w.days |= 1u << 5;
    CHECK(next(w, {}, at(kMon + 4, 8, 0), true).at == at(kMon + 5, 9, 30));
    // Every day off: nothing.
    w.days = 0;
    CHECK(next(w, {}, at(kMon, 0, 0), true).src == Src::None);
}

void test_alarm_one_day_on_is_a_week_away() {
    Week w = Week::daily(7 * 60);
    w.days = 1u << 2;  // Wednesdays only
    // Wednesday 07:01: the next is next Wednesday -- the eighth day of the scan.
    CHECK(next(w, {}, at(kMon + 2, 7, 1), true).at == at(kMon + 9, 7, 0));
    // ... and at 07:00 itself it is now.
    CHECK(due(w, {}, at(kMon + 2, 7, 0), true));
    CHECK(!due(w, {}, at(kMon + 3, 7, 0), true));
}

// The request, word for word: every day 07:00; Monday evening the knob says 08:00.  Tuesday
// rings at 08:00 and not at 07:00; Wednesday is 07:00 again; the week never changed.
void test_alarm_override_replaces_the_next_morning_only() {
    const Week w = Week::daily(7 * 60);
    const int64_t mon_evening = at(kMon, 21, 30);
    const Override o = override_at(w, mon_evening, 8 * 60, true);
    CHECK(o.pending() && o.at == at(kMon + 1, 8, 0) && o.day == kMon + 1);
    CHECK(w == Week::daily(7 * 60));

    const auto n = next(w, o, mon_evening, true);
    CHECK(n.src == Src::Override && n.at == at(kMon + 1, 8, 0));
    CHECK(!due(w, o, at(kMon + 1, 7, 0), true));  // Tuesday 07:00: silent
    CHECK(due(w, o, at(kMon + 1, 8, 0), true));   // Tuesday 08:00: rings

    // It rang; a minute later it is spent, and the replaced day still is not the week's.
    const Override after = expire(o, at(kMon + 1, 8, 1));
    CHECK(!after.pending() && after.day == kMon + 1);
    CHECK(next(w, after, at(kMon + 1, 8, 1), true).at == at(kMon + 2, 7, 0));
    // Wednesday: the day it replaced is over and it is gone entirely.
    CHECK(expire(after, at(kMon + 2, 0, 0)).empty());
    CHECK(due(w, expire(after, at(kMon + 2, 7, 0)), at(kMon + 2, 7, 0), true));
}

// Earlier the same morning: 06:00 on Monday, the knob says 06:30.  06:30 rings -- and 07:00
// must not ring after it, the override REPLACED today's alarm rather than adding one.
void test_alarm_override_the_same_morning_is_not_a_second_alarm() {
    const Week w = Week::daily(7 * 60);
    const Override o = override_at(w, at(kMon, 6, 0), 6 * 60 + 30, true);
    CHECK(o.at == at(kMon, 6, 30) && o.day == kMon);
    CHECK(due(w, o, at(kMon, 6, 30), true));
    const Override spent = expire(o, at(kMon, 6, 31));
    CHECK(!due(w, spent, at(kMon, 7, 0), true));
    CHECK(next(w, spent, at(kMon, 6, 31), true).at == at(kMon + 1, 7, 0));
}

// After today's alarm, a time later today is still today: 07:30, the knob says 07:45.
void test_alarm_override_later_today() {
    const Week w = Week::daily(7 * 60);
    const Override o = override_at(w, at(kMon, 7, 30), 7 * 60 + 45, true);
    CHECK(o.at == at(kMon, 7, 45));
    CHECK(next(w, expire(o, at(kMon, 7, 46)), at(kMon, 7, 46), true).at == at(kMon + 1, 7, 0));
}

// A weekend lie-in that the week does not have: Friday night, weekdays only, the knob says
// 09:00.  Saturday rings once; Monday is untouched.
void test_alarm_override_on_a_day_off() {
    Week w = Week::daily(7 * 60);
    w.days = 0x1F;
    const Override o = override_at(w, at(kMon + 4, 23, 0), 9 * 60, true);
    CHECK(o.at == at(kMon + 5, 9, 0) && o.day == kMon + 5);
    CHECK(next(w, o, at(kMon + 4, 23, 0), true).src == Src::Override);
    const Override spent = expire(o, at(kMon + 5, 9, 1));
    CHECK(next(w, spent, at(kMon + 5, 9, 1), true).at == at(kMon + 7, 7, 0));
}

// Setting the time the week already has is not an override: nothing to remember, nothing for
// the app to show as "one-off".
void test_alarm_override_that_matches_the_week_is_none() {
    const Week w = Week::daily(7 * 60);
    CHECK(override_at(w, at(kMon, 21, 0), 7 * 60, true).empty());
    // ... but the same minute on a day that is OFF is one.
    Week wd = w;
    wd.days = 0x1F;
    CHECK(override_at(wd, at(kMon + 4, 21, 0), 7 * 60, true).pending());
}

// The minute being set right now rings right now -- the edit ended inside it.
void test_alarm_override_this_minute() {
    const Week w = Week::daily(7 * 60);
    const Override o = override_at(w, at(kMon, 22, 15), 22 * 60 + 15, true);
    CHECK(o.at == at(kMon, 22, 15));
    CHECK(due(w, o, at(kMon, 22, 15), true));
}

// A clock set by the knob knows the hour and not the weekday.  A week that is the same every
// day still rings; any other week cannot be read, and only a one-off can ring.
void test_alarm_without_a_date() {
    const Week daily = Week::daily(7 * 60);
    CHECK(daily.uniform());
    CHECK(due(daily, {}, at(0, 7, 0), false));
    Week wd = daily;
    wd.days = 0x1F;
    CHECK(next(wd, {}, at(0, 6, 0), false).src == Src::None);
    const Override o = override_at(wd, at(0, 6, 0), 6 * 60 + 30, false);
    CHECK(o.pending() && due(wd, o, at(0, 6, 30), false));
    // The phone arrives with a real date: a 1970 one-off is behind us and is dropped.
    CHECK(expire(o, at(kMon, 6, 0)).empty());
}

// The clock is set forward past a pending one-off: it does not ring late, it is gone.
void test_alarm_override_jumped_over() {
    const Week w = Week::daily(7 * 60);
    const Override o = override_at(w, at(kMon, 22, 0), 8 * 60, true);
    const int64_t later = at(kMon + 1, 9, 0);
    CHECK(!due(w, o, later, true));
    CHECK(!expire(o, later).pending());
}

void test_alarm_week_validity() {
    Week w;
    CHECK(w.valid() && w.uniform() && w.min[0] == kDefaultMin);
    w.min[3] = kMinPerDay;
    CHECK(!w.valid());
    w.min[3] = 0;
    w.days = 0x80;
    CHECK(!w.valid());
}

}  // namespace

void run_alarm_tests() {
    test_alarm_calendar_arithmetic();
    test_alarm_week_skips_days_off();
    test_alarm_one_day_on_is_a_week_away();
    test_alarm_override_replaces_the_next_morning_only();
    test_alarm_override_the_same_morning_is_not_a_second_alarm();
    test_alarm_override_later_today();
    test_alarm_override_on_a_day_off();
    test_alarm_override_that_matches_the_week_is_none();
    test_alarm_override_this_minute();
    test_alarm_without_a_date();
    test_alarm_override_jumped_over();
    test_alarm_week_validity();
}
