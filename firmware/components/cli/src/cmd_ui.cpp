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
    struct NamedMode {
        const char* n;
        M m;
    };
    // The modes are named after the ICONS on the plate (README §12).  `setalarm`/`setclock`
    // are kept as aliases: they were the names until 2026-08-13 and are in fingers, scripts
    // and old test files -- but note that `alarm` now means the mode that SETS the alarm
    // time, where it used to mean the bell.
    constexpr NamedMode kModes[] = {
        {"idle", M::Idle},   {"bell", M::Bell},      {"alarm", M::Alarm},   {"setalarm", M::Alarm},
        {"clock", M::Clock}, {"setclock", M::Clock}, {"volume", M::Volume}, {"pairing", M::Pairing},
    };
    if (!w) {
        const auto s = svc::ui().snapshot();
        out.printf("mode %s   alarm %02d:%02d %s   vol %u%%", s.mode_name, s.alarm_hour,
                   s.alarm_minute, s.alarm_armed ? "armed" : "disarmed", s.volume);
        if (s.idle_in_ms) {
            out.printf("  back to idle in %" PRIu32 " ms without input", s.idle_in_ms);
        }
        if (s.net_locked) out.line("  clock mode is locked -- the network owns the time");
        return Status::Ok;
    }
    for (auto const& m : kModes) {
        if (std::strcmp(m.n, w) == 0) {
            svc::ui().set_mode(m.m);
            out.printf("mode %s", w);
            return Status::Ok;
        }
    }
    out.line("usage: ui mode <idle|bell|alarm|clock|volume|pairing>");
    out.line("  bell = arm/disarm · alarm = set its time · clock = set the time");
    return Status::BadArg;
}

Status cmd_knob(Args const& a, Sink& out) {
    auto t = svc::ui().tuning();
    if (a.count() < 2) {
        out.printf("counts_per_minute=%" PRId32 " slow_max=%" PRId32 " fast_at=%" PRId32
                   " accel_factor=%" PRId32 " deadband=%u",
                   t.counts_per_minute, t.slow_max, t.fast_at, t.accel_factor, t.arm_deadband);
        out.printf("timeout_ms=%" PRIu32 " long_press_ms=%" PRIu32 " pair_press_ms=%" PRIu32
                   " bright=%u%%",
                   t.timeout_ms, t.long_press_ms, t.pair_press_ms, t.brightness);
        out.line("  ui knob <counts|slow|fast|factor|deadband> <value>");
        out.line("  ui knob <timeout|longpress|pair|bright> <value>");
        return a.count() == 0 ? Status::Ok : Status::BadArg;
    }
    const char* k = a.arg(0);
    const auto v = static_cast<int32_t>(std::strtol(a.arg(1), nullptr, 10));
    if (std::strcmp(k, "counts") == 0) {
        t.counts_per_minute = v > 0 ? v : 1;
    } else if (std::strcmp(k, "slow") == 0 || std::strcmp(k, "threshold") == 0) {
        t.slow_max = v;
    } else if (std::strcmp(k, "fast") == 0) {
        t.fast_at = v;
    } else if (std::strcmp(k, "factor") == 0) {
        t.accel_factor = v > 0 ? v : 1;
    } else if (std::strcmp(k, "deadband") == 0) {
        t.arm_deadband = static_cast<uint8_t>(v < 1 ? 1 : (v > 64 ? 64 : v));
    } else if (std::strcmp(k, "timeout") == 0) {
        t.timeout_ms = static_cast<uint32_t>(v);
    } else if (std::strcmp(k, "longpress") == 0) {
        t.long_press_ms = static_cast<uint32_t>(v);
    } else if (std::strcmp(k, "pair") == 0) {
        t.pair_press_ms = static_cast<uint32_t>(v);
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

// Every duration the light has, in one place (domain/anim.hpp).  Changing one here changes
// it for every pattern that uses it, which is the point -- a breathing bell and a breathing
// battery warning are the same animation or they are an inconsistency.
Status cmd_anim(Args const& a, Sink& out) {
    auto c = svc::ui().anim_cfg();
    if (a.count() < 2) {
        out.printf("ramp_ms=%" PRIu32 " breathe_ms=%" PRIu32 " blink_ms=%" PRIu32 " duty=%u%%",
                   c.ramp_ms, c.breathe_ms, c.blink_ms, c.blink_duty);
        out.printf("flash_ms=%" PRIu32 " flash_gap_ms=%" PRIu32 " breathe_floor=%u", c.flash_ms,
                   c.flash_gap_ms, c.breathe_floor);
        out.printf("swell rise=%" PRIu32 " hold=%" PRIu32 " fall=%" PRIu32 " ms", c.swell_in_ms,
                   c.swell_hold_ms, c.swell_out_ms);
        out.line("  ui anim <ramp|breathe|blink|duty|flash|gap|floor> <value>");
        out.line("  ui anim <rise|hold|fall> <ms>   the tap's dial wash");
        return a.count() == 0 ? Status::Ok : Status::BadArg;
    }
    const char* k = a.arg(0);
    const auto v = static_cast<int32_t>(std::strtol(a.arg(1), nullptr, 10));
    if (v < 0) {
        out.line("durations are milliseconds and are not negative");
        return Status::BadArg;
    }
    const auto u32 = static_cast<uint32_t>(v);
    const auto u8 = static_cast<uint8_t>(v > 255 ? 255 : v);
    if (std::strcmp(k, "ramp") == 0) {
        c.ramp_ms = u32;
    } else if (std::strcmp(k, "breathe") == 0) {
        c.breathe_ms = u32;
    } else if (std::strcmp(k, "blink") == 0) {
        c.blink_ms = u32;
    } else if (std::strcmp(k, "duty") == 0) {
        c.blink_duty = static_cast<uint8_t>(v > 100 ? 100 : v);
    } else if (std::strcmp(k, "flash") == 0) {
        c.flash_ms = u32;
    } else if (std::strcmp(k, "gap") == 0) {
        c.flash_gap_ms = u32;
    } else if (std::strcmp(k, "floor") == 0) {
        c.breathe_floor = u8;
    } else if (std::strcmp(k, "rise") == 0) {
        c.swell_in_ms = u32;
    } else if (std::strcmp(k, "hold") == 0) {
        c.swell_hold_ms = u32;
    } else if (std::strcmp(k, "fall") == 0) {
        c.swell_out_ms = u32;
    } else {
        out.printf("no such timing '%s'", k);
        return Status::BadArg;
    }
    svc::ui().set_anim_cfg(c);
    out.printf("%s = %" PRId32, k, v);
    return Status::Ok;
}

// The room's light (§6.6g): how the TSL2591's lux scales every pixel.  Live, not persisted --
// the same as `ui knob` and `ui anim` until §7.5's one config exists.
Status cmd_room(Args const& a, Sink& out) {
    auto c = svc::ui().ambient_cfg();
    const char* k = a.arg(0);
    if (!k) {
        const auto u = svc::ui().snapshot();
        out.printf("room   %s   light %u%% of `bright`%s", c.on ? "on" : "OFF",
                   u.room_scale * 100u / 255u, c.on ? "" : " -- the room is ignored");
        if (u.room_lux < 0.0f)
            out.line("lux    no reading -- full brightness");
        else
            out.printf("lux    %.2f (the reading the level was last moved at)",
                       static_cast<double>(u.room_lux));
        out.printf("map    <= %.2f lux -> %u%%   >= %.1f lux -> 100%%   log between",
                   static_cast<double>(c.night_lux), c.night_pct, static_cast<double>(c.day_lux));
        out.printf("       hysteresis %.2f decades, slew %" PRIu32 " ms, stale after %" PRIu32
                   " ms",
                   static_cast<double>(c.hyst_dec), c.slew_ms, c.stale_ms);
        out.line("  ui room <on|off> | ui room <night|day> <lux> | ui room <floor> <pct>");
        out.line("  ui room <hyst> <decades> | ui room <slew> <ms>");
        return Status::Ok;
    }
    if (std::strcmp(k, "on") == 0 || std::strcmp(k, "off") == 0) {
        c.on = k[1] == 'n';
        svc::ui().set_ambient_cfg(c);
        out.printf("room %s", c.on ? "on" : "OFF -- full brightness whatever the room");
        return Status::Ok;
    }
    if (!a.arg(1)) {
        out.line("usage: ui room [<on|off> | <night|day|floor|hyst|slew> <value>]");
        return Status::BadArg;
    }
    const float v = std::strtof(a.arg(1), nullptr);
    if (!(v >= 0.0f)) {
        out.line("values are not negative");
        return Status::BadArg;
    }
    if (std::strcmp(k, "night") == 0) {
        c.night_lux = v;
    } else if (std::strcmp(k, "day") == 0) {
        c.day_lux = v;
    } else if (std::strcmp(k, "floor") == 0) {
        c.night_pct = static_cast<uint8_t>(v > 100.0f ? 100.0f : v);
    } else if (std::strcmp(k, "hyst") == 0) {
        c.hyst_dec = v;
    } else if (std::strcmp(k, "slew") == 0) {
        c.slew_ms = static_cast<uint32_t>(v);
    } else {
        out.printf("no such setting '%s'", k);
        return Status::BadArg;
    }
    if (!(c.day_lux > c.night_lux) || c.night_lux <= 0.0f) {
        out.printf("night (%.2f) must be above 0 and below day (%.2f) lux",
                   static_cast<double>(c.night_lux), static_cast<double>(c.day_lux));
        return Status::BadArg;
    }
    svc::ui().set_ambient_cfg(c);
    out.printf("%s = %.2f", k, static_cast<double>(v));
    return Status::Ok;
}

Status cmd_status(Args const&, Sink& out) {
    const auto u = svc::ui().snapshot();
    out.printf("mode   %s   alarm %02d:%02d %s   vol %u%%%s", u.mode_name, u.alarm_hour,
               u.alarm_minute, u.alarm_armed ? "armed" : "disarmed", u.volume,
               u.net_locked ? "   [clock locked: network owns the time]" : "");
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
    out.printf("room   light %u%%%s   faults on the row 0x%02x", u.room_scale * 100u / 255u,
               u.room_lux < 0.0f ? " (no reading)" : "", u.faults);
    out.printf("wake   warm=%u%% cool=%u%%", hal::wake::warm(), hal::wake::cool());
    const auto k = hal::knob::read();
    if (k.ok())
        out.printf("knob   count=%" PRId32 " sw=%d", k.v.count, k.v.sw ? 1 : 0);
    else
        out.printf("knob   %s", cmd::name(k.st));
    return Status::Ok;
}

// Bench isolation for the knob driver (§12.0.10).  Not `unsafe`: turning input OFF can only
// ever make a bench quieter, and turning it back on is what the product does anyway.
Status cmd_input(Args const& a, Sink& out) {
    const char* v = a.arg(0);
    if (!v) {
        out.printf("input %s", svc::ui().input() ? "on" : "OFF -- the knob drives nothing");
        return Status::Ok;
    }
    const bool on = std::strcmp(v, "on") == 0;
    if (!on && std::strcmp(v, "off") != 0) {
        out.line("usage: ui input [on|off]   (off = `ui` stops reading the knob entirely)");
        return Status::BadArg;
    }
    svc::ui().set_input(on);
    out.printf("input %s%s", on ? "on" : "OFF", on ? "" : " -- `sensor knob` still reads it");
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"ui", nullptr, "input", "[on|off]", "let the knob drive the UI, or isolate it", None,
     cmd_input},
    {"ui", nullptr, "status", "", "mode, pixels, wake duty, knob", ReleaseOk, cmd_status},
    {"ui", nullptr, "mode", "[<idle|bell|alarm|clock|volume|pairing>]", "the knob HSM", None,
     cmd_mode},
    {"ui", nullptr, "knob", "[<knob> <value>]", "sensitivity, timeouts, brightness", None,
     cmd_knob},
    {"ui", nullptr, "room", "[<setting> <value>]", "brightness follows the room (TSL2591)", None,
     cmd_room},
    {"ui", nullptr, "anim", "[<timing> <ms>]", "every LED animation duration", None, cmd_anim},
    {"ui", "led", "test", "", "walk the chain head to tail", Unsafe, cmd_led_test},
    {"ui", "led", "", "<id> <color|r g b w>", "set pixel(s)", Unsafe, cmd_led},
    {"ui", nullptr, "wake", "<warm%> <cool%>", "wake light duty (plugged-only)", Unsafe, cmd_wake},
};

}  // namespace

extern const CmdTable kTableUi{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
