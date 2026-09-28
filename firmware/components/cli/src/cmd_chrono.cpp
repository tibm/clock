// The `chrono` group.                                        [FIRMWARE.md §9.3, §6.4]
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "clk/cli/registry.hpp"
#include "clk/domain/hand.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/ui.hpp"

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
    out.printf("time   %02d:%02d:%02d local  %s%s%s", c.hour, c.minute, c.second,
               c.valid ? "" : "(never set)", c.tz_set ? off : "(no offset)",
               c.date_valid ? "" : "  (no date)");
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
    int off = svc::chrono().snapshot().tz_off_min;
    const bool off_ok = a.count() < 2 || parse_off(a.arg(1), off);
    if (!v || !end || *end || !off_ok || ms < 0) {
        out.line("usage: chrono time epoch <unix_ms> [<utc_offset_min>]   offset -720..840");
        return Status::BadArg;
    }
    if (ms < 100'000'000'000LL) {  // before 1973: that is seconds, not milliseconds
        out.printf("%lld looks like seconds -- want milliseconds since 1970 UTC", ms);
        return Status::BadArg;
    }
    svc::chrono().set_epoch(ms, off);
    const std::time_t t = static_cast<std::time_t>((ms + off * 60'000LL) / 1000);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char when[32], o[16];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
    fmt_off(o, sizeof o, off);
    out.printf("time %s local (%s) -- hands following", when, o);
    return Status::Ok;
}

Status cmd_tz(Args const& a, Sink& out) {
    char o[16];
    if (a.count() == 0) {
        const auto c = svc::chrono().snapshot();
        fmt_off(o, sizeof o, c.tz_off_min);
        out.printf("%s%s", o, c.tz_set ? "" : "  (never set -- UTC assumed)");
        return Status::Ok;
    }
    int off = 0;
    if (!parse_off(a.arg(0), off)) {
        out.line("usage: chrono tz <utc_offset_min>   -720..840, e.g. 120 for UTC+02:00");
        return Status::BadArg;
    }
    svc::chrono().set_tz(off);
    fmt_off(o, sizeof o, off);
    out.printf("%s -- same instant, the hands move", o);
    return Status::Ok;
}

// The alarm.  `ui` owns it (the knob edits the same two values), so these post and then wait
// for `ui` to say it took -- a CLI line that reports what it ASKED for is the F0.1 lie.
bool alarm_is(int min_of_day, int armed) {
    for (int i = 0; i < 60; ++i) {
        const auto u = svc::ui().snapshot();
        if ((min_of_day < 0 || u.alarm_hour * 60 + u.alarm_minute == min_of_day) &&
            (armed < 0 || u.alarm_armed == (armed != 0)))
            return true;
        hal::clock_::sleep_ms(5);
    }
    return false;
}

Status alarm_print(Sink& out) {
    const auto u = svc::ui().snapshot();
    out.printf("alarm %02d:%02d %s", u.alarm_hour, u.alarm_minute, u.alarm_armed ? "armed" : "off");
    return Status::Ok;
}

Status cmd_alarm(Args const& a, Sink& out) {
    if (a.count() == 0) return alarm_print(out);
    out.line("usage: chrono alarm [set <hh:mm> | arm <on|off>]");
    return Status::BadArg;
}

Status cmd_alarm_set(Args const& a, Sink& out) {
    int h = 0, m = 0, s = 0;
    if (!parse_time(a.arg(0), h, m, s)) {
        out.line("usage: chrono alarm set <hh:mm>");
        return Status::BadArg;
    }
    svc::ui().set_alarm(h * 60 + m);
    if (!alarm_is(h * 60 + m, -1)) {
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
    {"chrono", nullptr, "tz", "[<utc_offset_min>]", "UTC offset; the phone resends it on DST", None,
     cmd_tz},
    {"chrono", "alarm", "set", "<hh:mm>", "alarm time (NVS)", None, cmd_alarm_set},
    {"chrono", "alarm", "arm", "<on|off>", "arm or disarm the alarm (NVS)", None, cmd_alarm_arm},
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
