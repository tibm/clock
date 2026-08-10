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

Status cmd_follow(Args const& a, Sink& out) {
    const char* v = a.arg(0);
    const bool on = !v || std::strcmp(v, "on") == 0;
    svc::chrono().set_follow(on);
    out.printf("hands %s the clock", on ? "follow" : "are released from");
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"chrono", nullptr, "status", "", "time, hand target, alarm", ReleaseOk, cmd_status},
    {"chrono", nullptr, "time", "[set <hh:mm[:ss]>]", "read or set the wall clock", None, cmd_time},
    {"chrono", nullptr, "follow", "<on|off>", "let the hands track the clock", None, cmd_follow},
};

}  // namespace

extern const CmdTable kTableChrono{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
