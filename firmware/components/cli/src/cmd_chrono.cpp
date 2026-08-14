// The `chrono` group.                                        [FIRMWARE.md §9.3, §6.4]
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clk/cli/registry.hpp"
#include "clk/domain/hand.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/ui.hpp"

namespace clk::cli {
namespace {

// "07:30", "07:30:15", or an ISO "2026-08-09T06:59:30" of which only the time is used --
// there is no date in this system yet, and pretending otherwise would be a lie in a log.
bool parse_time(const char* s, int& h, int& m, int& sec) {
    if (!s) return false;
    if (const char* t = std::strchr(s, 'T')) s = t + 1;
    h = m = sec = 0;
    const int n = std::sscanf(s, "%d:%d:%d", &h, &m, &sec);
    return n >= 2 && h >= 0 && h < 24 && m >= 0 && m < 60 && sec >= 0 && sec < 60;
}

Status cmd_status(Args const&, Sink& out) {
    const auto c = svc::chrono().snapshot();
    out.printf("time   %02d:%02d:%02d  %s", c.hour, c.minute, c.second,
               c.valid ? "" : "(never set)");
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
    svc::chrono().set_time((h * 3600LL + m * 60 + s) * 1000LL);
    out.printf("time %02d:%02d:%02d -- hands following", h, m, s);
    return Status::Ok;
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
    {"chrono", nullptr, "time", "[set <hh:mm[:ss]>]", "read or set the wall clock", None, cmd_time},
    {"chrono", nullptr, "net", "[<fact> [on|off]]", "who owns the time: wifi + sntp", None,
     cmd_net},
    {"chrono", nullptr, "follow", "<on|off>", "let the hands track the clock", None, cmd_follow},
    {"chrono", nullptr, "steps", "[<1..60>]", "hand positions per minute: 1 ticks, 60 sweeps", None,
     cmd_steps},
};

}  // namespace

extern const CmdTable kTableChrono{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
