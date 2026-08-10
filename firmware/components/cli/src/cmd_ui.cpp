// The `ui` group -- pixels and the wake light.             [FIRMWARE.md §9.2, §9.3, §6.6]
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clk/board.hpp"
#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/services/ui.hpp"

namespace clk::cli {
namespace {

using hal::pixels::Rgbw;

// ---- pixel ids -------------------------------------------------------------------------
// Chain order is dial first: D40/D41 are on the main PCB (positions 1-2) and the five
// status pixels hang off J12 (positions 3-7).  README §9 had this backwards until
// 2026-08-09 -- get it wrong and `ui led test` looks broken when it is not.
struct PixelName {
    const char* name;
    int lo, hi;
};
constexpr PixelName kNames[] = {
    {"dial0", 0, 0}, {"dial1", 1, 1}, {"dial", 0, 1}, {"bell", 2, 2},   {"alarm", 3, 3},
    {"clock", 4, 4}, {"vol", 5, 5},   {"batt", 6, 6}, {"status", 2, 6}, {"all", 0, 6},
};

bool parse_id(const char* s, int& lo, int& hi) {
    if (!s) return false;
    for (auto const& n : kNames) {
        if (std::strcmp(n.name, s) == 0) {
            lo = n.lo;
            hi = n.hi;
            return true;
        }
    }
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (end && *end == '\0' && v >= 0 && v < static_cast<long>(hal::pixels::kCount)) {
        lo = hi = static_cast<int>(v);
        return true;
    }
    return false;
}

// ---- colours ---------------------------------------------------------------------------
struct Named {
    const char* name;
    Rgbw c;
};
constexpr Named kColors[] = {
    {"off", {0, 0, 0, 0}},         {"black", {0, 0, 0, 0}},        {"red", {255, 0, 0, 0}},
    {"green", {0, 255, 0, 0}},     {"blue", {0, 0, 255, 0}},       {"white", {0, 0, 0, 255}},
    {"warm", {255, 140, 20, 255}}, {"cool", {180, 200, 255, 255}}, {"cyan", {0, 255, 255, 0}},
    {"magenta", {255, 0, 255, 0}}, {"yellow", {255, 255, 0, 0}},   {"orange", {255, 120, 0, 0}},
    {"purple", {160, 0, 255, 0}},
};

uint8_t hex2(const char* p) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    const int h = nib(p[0]), l = nib(p[1]);
    return (h < 0 || l < 0) ? 0 : static_cast<uint8_t>(h * 16 + l);
}

Rgbw scale(Rgbw c, int pct) {
    auto s = [pct](uint8_t v) { return static_cast<uint8_t>(v * pct / 100); };
    return {s(c.r), s(c.g), s(c.b), s(c.w)};
}

// "red", "red@20", "#ff8800", "#ff8800c0"
bool parse_color(const char* s, Rgbw& out) {
    if (!s) return false;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s", s);
    int pct = 100;
    if (char* at = std::strchr(buf, '@')) {
        *at = '\0';
        pct = static_cast<int>(std::strtol(at + 1, nullptr, 10));
        if (pct < 0 || pct > 100) return false;
    }
    if (buf[0] == '#') {
        const std::size_t n = std::strlen(buf + 1);
        if (n != 6 && n != 8) return false;
        out = {hex2(buf + 1), hex2(buf + 3), hex2(buf + 5), n == 8 ? hex2(buf + 7) : uint8_t{0}};
        out = scale(out, pct);
        return true;
    }
    for (auto const& c : kColors) {
        if (std::strcmp(c.name, buf) == 0) {
            out = scale(c.c, pct);
            return true;
        }
    }
    return false;
}

// ---- commands --------------------------------------------------------------------------

Status cmd_led(Args const& a, Sink& out) {
    int lo = 0, hi = 0;
    if (!parse_id(a.arg(0), lo, hi)) {
        out.printf("bad pixel id '%s'", a.arg(0) ? a.arg(0) : "");
        out.line("  0-6 | dial0 dial1 dial | bell alarm clock vol batt | status | all");
        return Status::BadArg;
    }
    Rgbw c{};
    if (a.count() >= 5) {  // raw r g b w quad
        auto b = [&](int i) { return static_cast<uint8_t>(std::strtol(a.arg(i), nullptr, 10)); };
        c = {b(1), b(2), b(3), b(4)};
    } else if (a.count() >= 2) {
        if (!parse_color(a.arg(1), c)) {
            out.printf("bad colour '%s'", a.arg(1));
            out.line("  off red green blue white warm cool cyan magenta yellow orange purple");
            out.line("  | #RRGGBB | #RRGGBBWW    suffix @<pct> scales brightness, e.g. red@20");
            return Status::BadArg;
        }
    } else {
        out.line("usage: ui led <id> <color> | ui led <id> <r> <g> <b> <w>");
        return Status::BadArg;
    }

    for (int i = lo; i <= hi; ++i) {
        if (const Status st = hal::pixels::set(static_cast<std::size_t>(i), c); st != Status::Ok) {
            out.printf("pixel %d: %s", i, cmd::name(st));
            return st;
        }
    }
    if (const Status st = hal::pixels::refresh(); st != Status::Ok) return st;
    char id[40];
    if (lo == hi) {
        std::snprintf(id, sizeof id, "pixel %d", lo);
    } else {
        std::snprintf(id, sizeof id, "pixels %d-%d", lo, hi);
    }
    out.printf("%s  r=%u g=%u b=%u w=%u", id, c.r, c.g, c.b, c.w);
    return Status::Ok;
}

Status cmd_led_test(Args const&, Sink& out) {
    // Walks the chain head to tail.  This is what proves the SN74AHCT1G125 buffer and the
    // off-board J12 harness -- if pixels 3-7 stay dark, the harness is the suspect, not
    // the firmware (§12.1 milestone 4).
    if (!board::present(board::Dev::Pixels)) {
        out.line("pixels: not present");
        return Status::NotPresent;
    }
    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        hal::pixels::set_all(Rgbw{});
        hal::pixels::set(i, Rgbw{60, 60, 60, 0});
        hal::pixels::refresh();
        out.printf("pixel %zu  %s", i, i < 2 ? "dial (on-PCB)" : "status (via J12)");
        hal::clock_::sleep_ms(150);
    }
    hal::pixels::set_all(Rgbw{});
    return hal::pixels::refresh();
}

Status cmd_wake(Args const& a, Sink& out) {
    if (a.count() < 2) {
        out.line("usage: ui wake <warm%> <cool%>");
        return Status::BadArg;
    }
    const auto w = static_cast<int>(std::strtol(a.arg(0), nullptr, 10));
    const auto c = static_cast<int>(std::strtol(a.arg(1), nullptr, 10));
    if (w < 0 || w > 100 || c < 0 || c > 100) {
        out.line("percentages are 0-100");
        return Status::BadArg;
    }
    const Status st = hal::wake::set(static_cast<uint8_t>(w), static_cast<uint8_t>(c));
    if (st == Status::Denied) {
        // §6.8 interlock 4 -- the 12 V boost is plugged-only.
        out.line("denied: wake light is plugged-only (12 V boost gated on PD_PG)");
    }
    return st;
}

Status cmd_mode(Args const& a, Sink& out) {
    const char* w = a.arg(0);
    using M = svc::Ui::Mode;
    struct Named {
        const char* n;
        M m;
    };
    constexpr Named kModes[] = {{"idle", M::Idle},
                                {"alarm", M::Alarm},
                                {"setalarm", M::SetAlarm},
                                {"setclock", M::SetClock},
                                {"volume", M::Volume}};
    if (!w) {
        const auto s = svc::ui().snapshot();
        out.printf("mode %s   alarm %02d:%02d %s   vol %u%%", s.mode_name, s.alarm_hour,
                   s.alarm_minute, s.alarm_armed ? "armed" : "disarmed", s.volume);
        if (s.idle_in_ms) {
            out.printf("  back to idle in %" PRIu32 " ms without input", s.idle_in_ms);
        }
        return Status::Ok;
    }
    for (auto const& m : kModes) {
        if (std::strcmp(m.n, w) == 0) {
            svc::ui().set_mode(m.m);
            out.printf("mode %s", w);
            return Status::Ok;
        }
    }
    out.line("usage: ui mode <idle|alarm|setalarm|setclock|volume>");
    return Status::BadArg;
}

Status cmd_knob(Args const& a, Sink& out) {
    auto t = svc::ui().tuning();
    if (a.count() < 2) {
        out.printf("counts_per_minute=%" PRId32 " accel_threshold=%" PRId32 " accel_factor=%" PRId32
                   " timeout_ms=%" PRIu32 " long_press_ms=%" PRIu32 " bright=%u%%",
                   t.counts_per_minute, t.accel_threshold, t.accel_factor, t.timeout_ms,
                   t.long_press_ms, t.brightness);
        out.line("  ui knob <counts|threshold|factor|timeout|longpress|bright> <value>");
        return a.count() == 0 ? Status::Ok : Status::BadArg;
    }
    const char* k = a.arg(0);
    const auto v = static_cast<int32_t>(std::strtol(a.arg(1), nullptr, 10));
    if (std::strcmp(k, "counts") == 0) {
        t.counts_per_minute = v > 0 ? v : 1;
    } else if (std::strcmp(k, "threshold") == 0) {
        t.accel_threshold = v;
    } else if (std::strcmp(k, "factor") == 0) {
        t.accel_factor = v > 0 ? v : 1;
    } else if (std::strcmp(k, "timeout") == 0) {
        t.timeout_ms = static_cast<uint32_t>(v);
    } else if (std::strcmp(k, "longpress") == 0) {
        t.long_press_ms = static_cast<uint32_t>(v);
    } else if (std::strcmp(k, "bright") == 0) {
        t.brightness = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
    } else {
        out.printf("no such knob '%s'", k);
        return Status::BadArg;
    }
    svc::ui().set_tuning(t);
    out.printf("%s = %" PRId32, k, v);
    return Status::Ok;
}

Status cmd_status(Args const&, Sink& out) {
    const auto u = svc::ui().snapshot();
    out.printf("mode   %s   alarm %02d:%02d %s   vol %u%%", u.mode_name, u.alarm_hour,
               u.alarm_minute, u.alarm_armed ? "armed" : "disarmed", u.volume);
    char px[hal::pixels::kCount + 1] = {};
    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        const auto c = hal::pixels::get(i);
        px[i] = (c.r || c.g || c.b || c.w) ? '#' : '.';
    }
    out.printf("pixels [%s]   dial=%c%c status=%c%c%c%c%c", px, px[0], px[1], px[2], px[3], px[4],
               px[5], px[6]);
    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        const auto c = hal::pixels::get(i);
        if (c.r || c.g || c.b || c.w) {
            out.printf("  [%zu] r=%u g=%u b=%u w=%u", i, c.r, c.g, c.b, c.w);
        }
    }
    out.printf("wake   warm=%u%% cool=%u%%", hal::wake::warm(), hal::wake::cool());
    const auto k = hal::knob::read();
    if (k.ok())
        out.printf("knob   count=%" PRId32 " sw=%d", k.v.count, k.v.sw ? 1 : 0);
    else
        out.printf("knob   %s", cmd::name(k.st));
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"ui", nullptr, "status", "", "mode, pixels, wake duty, knob", ReleaseOk, cmd_status},
    {"ui", nullptr, "mode", "[<idle|alarm|setalarm|setclock|volume>]", "the knob HSM", None,
     cmd_mode},
    {"ui", nullptr, "knob", "[<knob> <value>]", "sensitivity, timeouts, brightness", None,
     cmd_knob},
    {"ui", "led", "test", "", "walk the chain head to tail", Unsafe, cmd_led_test},
    {"ui", "led", "", "<id> <color|r g b w>", "set pixel(s)", Unsafe, cmd_led},
    {"ui", nullptr, "wake", "<warm%> <cool%>", "wake light duty (plugged-only)", Unsafe, cmd_wake},
};

}  // namespace

extern const CmdTable kTableUi{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
