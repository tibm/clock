// The `chrono` group.                                        [FIRMWARE.md §9.3, §6.4]
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "clk/cli/registry.hpp"
#include "clk/domain/alarm.hpp"
#include "clk/domain/hand.hpp"
#include "clk/domain/tz.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/storage.hpp"
#include "clk/services/ui.hpp"
#include "sto_wait.hpp"

namespace clk::cli {
namespace {

// "07:30", "07:30:15", or an ISO "2026-08-09T06:59:30" of which only the time is used -- a
// DATE only ever arrives whole, as `chrono time epoch`, never guessed from a string.
bool parse_time(const char* s, int& h, int& m, int& sec) {
    if (!s) return false;
    if (const char* t = std::strchr(s, 'T')) s = t + 1;
    h = m = sec = 0;
    const int n = std::sscanf(s, "%d:%d:%d", &h, &m, &sec);
    return n >= 2 && h >= 0 && h < 24 && m >= 0 && m < 60 && sec >= 0 && sec < 60;
}

// "+02:00" style, for humans.
void fmt_off(char* buf, std::size_t cap, int off) {
    const int a = off < 0 ? -off : off;
    std::snprintf(buf, cap, "UTC%c%02d:%02d", off < 0 ? '-' : '+', a / 60, a % 60);
}

bool parse_off(const char* s, int& off) {
    if (!s) return false;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (!end || *end || v < svc::Chrono::kTzMinMin || v > svc::Chrono::kTzMaxMin) return false;
    off = static_cast<int>(v);
    return true;
}

Status cmd_status(Args const&, Sink& out) {
    const auto c = svc::chrono().snapshot();
    char off[16];
    fmt_off(off, sizeof off, c.tz_off_min);
    out.printf("time   %02d:%02d:%02d local  %s%s%s%s  %s%s", c.hour, c.minute, c.second,
               c.valid ? "" : "(never set) ", off, c.tz_dst ? " DST" : "",
               c.date_valid ? "" : "  (no date)", c.valid ? "set by " : "",
               c.valid ? svc::Chrono::name(c.src) : "");
    out.printf("zone   %s%s%s%s", c.tz_posix, c.tz_name[0] ? "  = " : "", c.tz_name,
               c.tz_set ? "" : "  (default)");
    out.printf("hands  %s   target h=%" PRId32 " m=%" PRId32,
               c.follow ? "following the clock" : "released (knob preview)", c.target_hour,
               c.target_minute);
    out.printf("steps  %d per minute  (one move every %.1f s of clock time)",
               svc::chrono().steps_per_minute(), 60.0 / svc::chrono().steps_per_minute());
    const auto u = svc::ui().snapshot();
    out.printf("alarm  %02d:%02d  %s", u.alarm_hour, u.alarm_minute,
               u.alarm_armed ? "armed" : "disarmed");
    return Status::Ok;
}

Status cmd_time(Args const& a, Sink& out) {
    if (a.count() == 0) {
        const auto c = svc::chrono().snapshot();
        out.printf("%02d:%02d:%02d", c.hour, c.minute, c.second);
        return Status::Ok;
    }
    const char* v = a.arg(0);
    if (std::strcmp(v, "set") == 0) v = a.arg(1);
    int h = 0, m = 0, s = 0;
    if (!parse_time(v, h, m, s)) {
        out.line("usage: chrono time [set] <hh:mm[:ss]>   (or an ISO timestamp)");
        return Status::BadArg;
    }
    svc::chrono().set_local_time(h, m, s);
    out.printf("time %02d:%02d:%02d local -- hands following", h, m, s);
    return Status::Ok;
}

// `chrono time epoch <unix_ms> [<utc_offset_min>]` -- the phone's form: a whole instant with
// its date, and the offset in force now.  Milliseconds, and a value that only makes sense as
// SECONDS is refused rather than taken as a date in January 1970.
Status cmd_epoch(Args const& a, Sink& out) {
    const char* v = a.arg(0);
    char* end = nullptr;
    const long long ms = v ? std::strtoll(v, &end, 10) : -1;
    int off = 0;
    const bool off_ok = a.count() < 2 || parse_off(a.arg(1), off);
    if (!v || !end || *end || !off_ok || ms < 0) {
        out.line("usage: chrono time epoch <unix_ms> [<utc_offset_min>]   offset -720..840");
        return Status::BadArg;
    }
    if (ms < 100'000'000'000LL) {  // before 1973: that is seconds, not milliseconds
        out.printf("%lld looks like seconds -- want milliseconds since 1970 UTC", ms);
        return Status::BadArg;
    }
    if (a.count() < 2) {
        // No offset: the zone decides.  The offset shown is the one it gives this instant.
        svc::chrono().set_utc(ms, svc::Chrono::Source::Phone);
        const svc::Chrono& c = svc::chrono();
        domain::tz::Zone z{};
        (void)domain::tz::parse(c.snapshot().tz_posix, z);
        off = domain::tz::offset_s(z, ms / 1000) / 60;
    } else {
        svc::chrono().set_epoch(ms, off);
    }
    const std::time_t t = static_cast<std::time_t>((ms + off * 60'000LL) / 1000);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char when[32], o[16];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
    fmt_off(o, sizeof o, off);
    out.printf("time %s local (%s) -- hands following", when, o);
    return Status::Ok;
}

// `chrono tz` -- a zone.  Three forms: nothing (show it), minutes (a fixed offset, the old
// form, e.g. `120`), or a POSIX rule with an optional label -- the app's form, e.g.
// `chrono tz PST8PDT,M3.2.0,M11.1.0 America/Los_Angeles`.
Status cmd_tz(Args const& a, Sink& out) {
    char o[16];
    if (a.count() == 0) {
        const auto c = svc::chrono().snapshot();
        fmt_off(o, sizeof o, c.tz_off_min);
        out.printf("%s%s   %s%s%s%s", o, c.tz_dst ? " DST" : "", c.tz_posix,
                   c.tz_name[0] ? "  = " : "", c.tz_name,
                   c.tz_set ? "" : "  (default -- San Francisco until the phone says)");
        out.kv("posix", c.tz_posix);
        out.kv("name", c.tz_name);
        char v[12];
        std::snprintf(v, sizeof v, "%d", c.tz_off_min);
        out.kv("offset", v);
        return Status::Ok;
    }
    int off = 0;
    if (parse_off(a.arg(0), off) && a.count() == 1) {
        svc::chrono().set_tz(off);
        fmt_off(o, sizeof o, off);
        out.printf("%s (fixed) -- same instant, the hands move", o);
        return Status::Ok;
    }
    const char* label = a.count() > 1 ? a.arg(1) : "";
    if (a.count() > 2 || std::strlen(label) >= sizeof(svc::Chrono::Snapshot{}.tz_name) ||
        !svc::chrono().set_zone(a.arg(0), label)) {
        out.line("usage: chrono tz [<utc_offset_min> | <posix_tz> [<name>]]");
        out.line("  e.g. `chrono tz 120` (fixed UTC+02:00), or");
        out.line("       `chrono tz CET-1CEST,M3.5.0,M10.5.0/3 Europe/Zurich`");
        return Status::BadArg;
    }
    const auto c = svc::chrono().snapshot();
    domain::tz::Zone z{};
    (void)domain::tz::parse(a.arg(0), z);
    const int now_off = domain::tz::offset_s(z, c.epoch_ms / 1000) / 60;
    fmt_off(o, sizeof o, now_off);
    out.printf("zone %s%s%s -- %s now; same instant, the hands move", a.arg(0), *label ? " = " : "",
               label, o);
    return Status::Ok;
}

// The alarm.  `ui` owns it (the knob edits the next one), so these post and then wait for `ui`
// to say it took -- a CLI line that reports what it ASKED for is the F0.1 lie.
template <class Pred>
bool ui_took(Pred p) {
    for (int i = 0; i < 60; ++i) {
        if (p(svc::ui().snapshot())) return true;
        hal::clock_::sleep_ms(5);
    }
    return false;
}

bool alarm_is(int min_of_day, int armed) {
    return ui_took([&](svc::Ui::Snapshot const& u) {
        return (min_of_day < 0 || u.alarm_hour * 60 + u.alarm_minute == min_of_day) &&
               (armed < 0 || u.alarm_armed == (armed != 0));
    });
}

namespace al = domain::alarm;

// "mon".."sun", "weekdays", "weekend", "all" -> a day mask; 0 = not a day.
uint8_t parse_days(std::string_view v) {
    if (v == "all" || v == "daily") return al::kAllDays;
    if (v == "weekdays") return 0x1F;
    if (v == "weekend") return 0x60;
    for (int d = 0; d < al::kDays; ++d)
        if (v == al::day_name(d)) return static_cast<uint8_t>(1u << d);
    return 0;
}

// One day of `chrono alarm week`: "07:00" rings, "-07:00" is off at 07:00, "-" is off and
// keeps the time the clock already has for that day.
bool parse_week_day(const char* v, al::Week& w, int d) {
    if (!v) return false;
    const bool off = v[0] == '-';
    if (off) ++v;
    if (off && !*v) {
        w.days = static_cast<uint8_t>(w.days & ~(1u << d));
        return true;
    }
    int h = 0, m = 0, sec = 0;
    if (!parse_time(v, h, m, sec) || sec != 0) return false;
    w.min[d] = static_cast<int16_t>(h * 60 + m);
    w.days =
        off ? static_cast<uint8_t>(w.days & ~(1u << d)) : static_cast<uint8_t>(w.days | (1u << d));
    return true;
}

Status alarm_print(Sink& out) {
    const auto u = svc::ui().snapshot();
    const auto s = svc::storage().snapshot();
    const char* what = u.alarm_next == al::Src::Override   ? "  (one-off, the week is unchanged)"
                       : u.alarm_next == al::Src::Schedule ? ""
                                                           : "  (nothing coming: every day off)";
    out.printf("alarm %02d:%02d %s%s%s%s  tone %s", u.alarm_hour, u.alarm_minute,
               u.alarm_armed ? "armed" : "off", u.alarm_next_wday >= 0 ? " next " : "",
               u.alarm_next_wday >= 0 ? al::day_name(u.alarm_next_wday) : "", what,
               s.alarm_tone[0] ? s.alarm_tone : "(beep)");
    char wk[96];
    int n = 0;
    for (int d = 0; d < al::kDays; ++d)
        n += std::snprintf(wk + n, sizeof wk - static_cast<std::size_t>(n), " %s %s%02d:%02d",
                           al::day_name(d), u.alarm_week.on(d) ? "" : "-", u.alarm_week.min[d] / 60,
                           u.alarm_week.min[d] % 60);
    out.printf("week %s", wk);
    if (u.mode == svc::Ui::Mode::Ringing)
        out.printf("  RINGING for %lu s -- `chrono alarm snooze|dismiss`",
                   static_cast<unsigned long>(u.ringing_ms / 1000));
    if (u.mode == svc::Ui::Mode::Snoozed)
        out.printf("  snoozed, rings again in %lu s",
                   static_cast<unsigned long>(u.snooze_left_ms / 1000));
    return Status::Ok;
}

Status cmd_alarm(Args const& a, Sink& out) {
    if (a.count() == 0) return alarm_print(out);
    out.line("usage: chrono alarm [set <hh:mm> | week <mon>..<sun> | day <day> <hh:mm|on|off> |");
    out.line("                     next <hh:mm|clear> | arm <on|off> | tone [<name>|none]]");
    return Status::BadArg;
}

// Which file rings.  The AO opens it and checks the header before it is accepted, so a file
// that would not play is refused NOW rather than at 07:00.
Status cmd_alarm_tone(Args const& a, Sink& out) {
    if (!a.arg(0)) {
        const auto s = svc::storage().snapshot();
        out.printf("tone %s", s.alarm_tone[0] ? s.alarm_tone : "(none -- the beep)");
        out.line(
            "  `storage ls` lists the candidates; `chrono alarm tone none` goes back to the beep");
        return Status::Ok;
    }
    const bool none = a.sv(0) == "none";
    if (const Status st = sto_await(svc::storage().select_tone(none ? "" : a.arg(0)), "tone", out);
        st != Status::Ok)
        return st;
    return alarm_print(out);
}

bool mode_is(svc::Ui::Mode m) {
    for (int i = 0; i < 60; ++i) {
        if (svc::ui().snapshot().mode == m) return true;
        hal::clock_::sleep_ms(5);
    }
    return false;
}

// The three things the knob and a tap do to an alarm, from the console and the app.
Status cmd_alarm_fire(Args const&, Sink& out) {
    svc::ui().fire_alarm();
    if (!mode_is(svc::Ui::Mode::Ringing)) {
        out.line("ui did not take it (not running?)");
        return Status::Failed;
    }
    // `storage` answers the ring request on its own thread; give it the moment it needs to
    // pick a voice, so the line below says which one it picked.
    hal::clock_::sleep_ms(50);
    const auto s = svc::storage().snapshot();
    if (s.playing == svc::Storage::Playing::Beep) {
        out.printf("ringing -- the fallback beep (%s)", s.last_why ? s.last_why : "?");
    } else if (s.playing == svc::Storage::Playing::File) {
        out.printf("ringing -- %s, ramping up over %lu s", s.file,
                   static_cast<unsigned long>(svc::Storage::kAlarmRampMs / 1000));
    } else {
        out.line(
            "ringing -- silently (no sound source answered: `storage status`, `audio status`)");
    }
    return Status::Ok;
}

Status cmd_alarm_snooze(Args const&, Sink& out) {
    if (svc::ui().snapshot().mode != svc::Ui::Mode::Ringing) {
        out.line("snooze refused: the alarm is not ringing");
        return Status::NotReady;
    }
    svc::ui().snooze();
    if (!mode_is(svc::Ui::Mode::Snoozed)) {
        out.line("ui did not take it (not running?)");
        return Status::Failed;
    }
    return alarm_print(out);
}

Status cmd_alarm_dismiss(Args const&, Sink& out) {
    const auto m = svc::ui().snapshot().mode;
    if (m != svc::Ui::Mode::Ringing && m != svc::Ui::Mode::Snoozed) {
        out.line("dismiss refused: the alarm is not ringing or snoozed");
        return Status::NotReady;
    }
    svc::ui().dismiss();
    if (!mode_is(svc::Ui::Mode::Idle)) {
        out.line("ui did not take it (not running?)");
        return Status::Failed;
    }
    out.line("alarm dismissed -- still armed for tomorrow");
    return Status::Ok;
}

Status cmd_alarm_set(Args const& a, Sink& out) {
    int h = 0, m = 0, s = 0;
    if (!parse_time(a.arg(0), h, m, s)) {
        out.line("usage: chrono alarm set <hh:mm>");
        return Status::BadArg;
    }
    svc::ui().set_alarm(h * 60 + m);
    const auto want = al::Week::daily(static_cast<int16_t>(h * 60 + m));
    if (!ui_took([&](svc::Ui::Snapshot const& u) {
            return u.alarm_week == want && u.alarm_ovr.empty();
        })) {
        out.line("ui did not take it (not running?)");
        return Status::Failed;
    }
    return alarm_print(out);
}

Status week_apply(al::Week const& w, Sink& out) {
    svc::ui().set_week(w);
    if (!ui_took([&](svc::Ui::Snapshot const& u) { return u.alarm_week == w; })) {
        out.line("ui did not take it (not running?)");
        return Status::Failed;
    }
    return alarm_print(out);
}

// The phone's whole week in one line, Monday first: seven of `hh:mm`, `-hh:mm`, `-`.
Status cmd_alarm_week(Args const& a, Sink& out) {
    if (a.count() != al::kDays) {
        out.line("usage: chrono alarm week <mon> <tue> <wed> <thu> <fri> <sat> <sun>");
        out.line("  each hh:mm (rings) | -hh:mm (off, keeps the time) | - (off, time unchanged)");
        return Status::BadArg;
    }
    al::Week w = svc::ui().snapshot().alarm_week;
    for (int d = 0; d < al::kDays; ++d) {
        if (!parse_week_day(a.arg(d), w, d)) {
            out.printf("bad %s: '%s'", al::day_name(d), a.arg(d));
            return Status::BadArg;
        }
    }
    return week_apply(w, out);
}

// One day (or a group) of the week, for a person at the console.
Status cmd_alarm_day(Args const& a, Sink& out) {
    const uint8_t mask = a.arg(0) ? parse_days(a.sv(0)) : 0;
    const auto v = a.sv(1);
    int h = 0, m = 0, sec = 0;
    const bool is_time = a.arg(1) && parse_time(a.arg(1), h, m, sec) && sec == 0;
    if (!mask || (!is_time && v != "on" && v != "off")) {
        out.line("usage: chrono alarm day <mon..sun|weekdays|weekend|all> <hh:mm|on|off>");
        return Status::BadArg;
    }
    al::Week w = svc::ui().snapshot().alarm_week;
    for (int d = 0; d < al::kDays; ++d) {
        if (!((mask >> d) & 1u)) continue;
        if (is_time) w.min[d] = static_cast<int16_t>(h * 60 + m);
        if (v == "off")
            w.days = static_cast<uint8_t>(w.days & ~(1u << d));
        else
            w.days = static_cast<uint8_t>(w.days | (1u << d));
    }
    return week_apply(w, out);
}

// The knob's one-off: the next alarm rings at hh:mm, once; the week is untouched.
Status cmd_alarm_next(Args const& a, Sink& out) {
    if (a.sv(0) == "clear") {
        svc::ui().set_next(-1);
        if (!ui_took([](svc::Ui::Snapshot const& u) { return u.alarm_ovr.empty(); })) {
            out.line("ui did not take it (not running?)");
            return Status::Failed;
        }
        return alarm_print(out);
    }
    int h = 0, m = 0, sec = 0;
    if (!parse_time(a.arg(0), h, m, sec)) {
        out.line("usage: chrono alarm next <hh:mm|clear>");
        return Status::BadArg;
    }
    svc::ui().set_next(h * 60 + m);
    if (!ui_took([&](svc::Ui::Snapshot const& u) {
            return u.alarm_next != al::Src::None &&
                   u.alarm_hour * 60 + u.alarm_minute == h * 60 + m;
        })) {
        out.line("ui did not take it (not running?)");
        return Status::Failed;
    }
    return alarm_print(out);
}

Status cmd_alarm_arm(Args const& a, Sink& out) {
    const auto v = a.sv(0);
    if (v != "on" && v != "off") {
        out.line("usage: chrono alarm arm <on|off>");
        return Status::BadArg;
    }
    svc::ui().arm_alarm(v == "on");
    if (!alarm_is(-1, v == "on" ? 1 : 0)) {
        out.line("ui did not take it (not running?)");
        return Status::Failed;
    }
    return alarm_print(out);
}

// How the running clock is RENDERED onto the hands, which is a separate question from how
// finely the knob edits it (`ui knob counts`).  A real quartz movement ticks once a second;
// a good mechanical one sweeps.  This dial can do either, and the answer is a house style
// rather than a fact about the mechanism.
Status cmd_steps(Args const& a, Sink& out) {
    if (a.count() == 0) {
        const int n = svc::chrono().steps_per_minute();
        out.printf("steps_per_minute %d  (%s)", n,
                   n == 1    ? "one jump a minute -- a ticking clock"
                   : n >= 60 ? "once a second"
                             : "between the two");
        return Status::Ok;
    }
    const long n = std::strtol(a.arg(0), nullptr, 10);
    if (n < 1 || n > 60) {
        out.line("usage: chrono steps <1..60>   (positions per minute: 1 ticks, 60 sweeps)");
        out.line("  the wall clock is unaffected -- this is only how often the hands are moved");
        return Status::BadArg;
    }
    svc::chrono().set_steps_per_minute(static_cast<int>(n));
    out.printf("steps_per_minute %ld  -- one move every %.1f s of clock time", n, 60.0 / n);
    return Status::Ok;
}

Status cmd_follow(Args const& a, Sink& out) {
    const char* v = a.arg(0);
    const bool on = !v || std::strcmp(v, "on") == 0;
    svc::chrono().set_follow(on);
    out.printf("hands %s the clock", on ? "follow" : "are released from");
    return Status::Ok;
}

// Who owns the time.  `net` (§6.7) will report this; until it exists the two facts are set
// here, and they are the difference between a `clock` mode that works and one that flashes
// red and skips (README §12).  Not host-only: on the board this is how you check the
// interlock without waiting for a real SNTP round trip.
Status cmd_net(Args const& a, Sink& out) {
    const auto c = svc::chrono().snapshot();
    if (a.count() == 0) {
        out.printf("wifi   %s", c.net_provisioned ? "provisioned" : "not provisioned");
        out.printf("sntp   %s", c.net_synced ? "synced" : "never synced");
        out.printf("clock  %s", svc::ui().snapshot().net_locked
                                    ? "LOCKED -- the knob may not set the time"
                                    : "settable by the knob");
        out.line("  chrono net <provisioned|synced|none|both> [on|off]");
        return Status::Ok;
    }
    const char* k = a.arg(0);
    const char* v = a.arg(1);
    const bool on = !v || std::strcmp(v, "on") == 0 || std::strcmp(v, "1") == 0;
    bool prov = c.net_provisioned, sync = c.net_synced;
    if (std::strcmp(k, "provisioned") == 0) {
        prov = on;
    } else if (std::strcmp(k, "synced") == 0) {
        sync = on;
    } else if (std::strcmp(k, "both") == 0) {
        prov = sync = on;
    } else if (std::strcmp(k, "none") == 0) {
        prov = sync = false;
    } else {
        out.line("usage: chrono net <provisioned|synced|none|both> [on|off]");
        return Status::BadArg;
    }
    // Synced without provisioned is not a state the product can be in; refusing to model it
    // keeps the lock a single readable condition rather than three.
    if (sync && !prov) prov = true;
    svc::chrono().set_net(prov, sync);
    out.printf("wifi %s, sntp %s", prov ? "provisioned" : "not provisioned",
               sync ? "synced" : "never synced");
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"chrono", nullptr, "status", "", "time, hand target, alarm", ReleaseOk, cmd_status},
    {"chrono", nullptr, "time", "[set <hh:mm[:ss]>]", "read or set the local time of day", None,
     cmd_time},
    {"chrono", "time", "epoch", "<unix_ms> [<utc_offset_min>]", "set date + time (UTC) + offset",
     None, cmd_epoch},
    {"chrono", nullptr, "tz", "[<offset_min> | <posix> [<name>]]",
     "zone: POSIX rule (NVS); default San Francisco", None, cmd_tz},
    {"chrono", "alarm", "set", "<hh:mm>", "every day at hh:mm, all days on (NVS)", None,
     cmd_alarm_set},
    {"chrono", "alarm", "week", "<mon> .. <sun>", "the week: hh:mm | -hh:mm | - each (NVS)", None,
     cmd_alarm_week},
    {"chrono", "alarm", "day", "<day|weekdays|weekend|all> <hh:mm|on|off>", "one day of the week",
     None, cmd_alarm_day},
    {"chrono", "alarm", "next", "<hh:mm|clear>", "the next alarm only, once -- as the knob sets it",
     None, cmd_alarm_next},
    {"chrono", "alarm", "arm", "<on|off>", "arm or disarm the alarm (NVS)", None, cmd_alarm_arm},
    {"chrono", "alarm", "tone", "[<name>|none]", "which /sd/tones WAV rings (NVS)", None,
     cmd_alarm_tone},
    {"chrono", "alarm", "fire", "", "ring now -- hear the alarm", None, cmd_alarm_fire},
    {"chrono", "alarm", "snooze", "", "what a press or a tap does to a ring", ReleaseOk,
     cmd_alarm_snooze},
    {"chrono", "alarm", "dismiss", "", "what a long press does to a ring", ReleaseOk,
     cmd_alarm_dismiss},
    {"chrono", "alarm", "", "", "show the alarm", ReleaseOk, cmd_alarm},
    {"chrono", nullptr, "net", "[<fact> [on|off]]", "who owns the time: wifi + sntp", None,
     cmd_net},
    {"chrono", nullptr, "follow", "<on|off>", "let the hands track the clock", None, cmd_follow},
    {"chrono", nullptr, "steps", "[<1..60>]", "hand positions per minute: 1 ticks, 60 sweeps", None,
     cmd_steps},
};

}  // namespace

extern const CmdTable kTableChrono{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
