// The `sensor` group -- read-only, always safe, present in release builds.
// [FIRMWARE.md §9.5]
//
// This is a VIEW, not a driver: every sample goes through hal/, and when the AOs land it
// will go through the owning AO instead.  Nothing here touches a peripheral register.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clk/board.hpp"
#include "clk/cli/registry.hpp"
#include "clk/cli/stream.hpp"
#include "clk/hal/hal.hpp"
#include "clk/log.hpp"

namespace clk::cli {
namespace {

struct SensorSpec {
    const char* name;
    board::Dev dev;
    uint16_t max_hz;
    bool has_driver;  // false = the PART may well be fitted, but no driver reads it
    const char* what;
    Status (*sample)(char* out, std::size_t cap);
};

// ---- samplers --------------------------------------------------------------------------

Status s_homing(char* out, std::size_t cap) {
    const auto mv = hal::adc::read_mv(hal::adc::Ch::Opto);
    if (!mv.ok()) return mv.st;
    const auto n = hal::adc::read_opto_norm();
    std::snprintf(out, cap, "mv=%u norm=%.3f", mv.v, n.ok() ? static_cast<double>(n.v) : 0.0);
    return Status::Ok;
}

Status s_vbat(char* out, std::size_t cap) {
    const auto p = hal::power::read();
    if (!p.ok()) return p.st;
    std::snprintf(out, cap, "mv=%u soc=%u plugged=%d chrg=%d", p.v.vbat_mv, p.v.soc_pct,
                  p.v.plugged ? 1 : 0, p.v.charging ? 1 : 0);
    return Status::Ok;
}

Status s_knob(char* out, std::size_t cap) {
    const auto k = hal::knob::read();
    if (!k.ok()) return k.st;
    std::snprintf(out, cap, "count=%" PRId32 " delta=%" PRId32 " sw=%d", k.v.count, k.v.delta,
                  k.v.sw ? 1 : 0);
    return Status::Ok;
}

Status s_clk(char* out, std::size_t cap) {
    // The real slow-clock source check (§7.1) arrives with `chrono`; for now this reports
    // the monotonic base the whole system schedules on, which is the thing you would be
    // staring at while debugging a warped simulation.
    std::snprintf(out, cap, "sim_ms=%" PRIu32 " src=%s", hal::clock_::millis(),
                  board::present(board::Dev::Xtal32k) ? "XTAL32K" : "INT_RC");
    return Status::Ok;
}

// Registered so `help`, `sensor list` and the docs agree, but not readable until the
// drivers land.  NotPresent is the honest answer and costs one quiet line (D16).
Status s_needs_driver(char*, std::size_t) { return Status::NotPresent; }

// `has_driver == false` is NOT the same as the part being absent, and conflating the two
// would cost bench time: "nothing is reading it yet" and "it is not plugged in" send you to
// different places.  `sensor list` prints them differently for exactly that reason.
constexpr SensorSpec kSensors[] = {
    {"homing", board::Dev::Opto, 200, true, "QRE1113 opto: mV + normalised", s_homing},
    {"knob", board::Dev::Knob, 50, true, "PCNT count, delta, ENC_SW", s_knob},
    {"vbat", board::Dev::Vbat, 10, true, "cell mV, SoC, charger state", s_vbat},
    {"clk", board::Dev::Xtal32k, 1, true, "slow-clock source, sim time", s_clk},
    {"als", board::Dev::Als, 10, false, "TSL2591 lux", s_needs_driver},
    {"env", board::Dev::Env, 1, false, "BME688 T/RH/P/IAQ", s_needs_driver},
    {"imu", board::Dev::Imu, 1, false, "BNO085 taps", s_needs_driver},
    {"exp", board::Dev::Expander, 20, false, "MCP23017 ports", s_needs_driver},
    {"amp", board::Dev::Amp, 10, false, "TAS5760M faults", s_needs_driver},
};
const SensorSpec* find_sensor(const char* n) {
    if (!n) return nullptr;
    for (auto const& s : kSensors) {
        if (std::strcmp(s.name, n) == 0) return &s;
    }
    return nullptr;
}

// ---- commands --------------------------------------------------------------------------

Status cmd_list(Args const&, Sink& out) {
    out.printf("board %s        name     state     last", board::board_name());
    char buf[96];
    for (auto const& s : kSensors) {
        if (!board::present(s.dev)) {
            out.printf("  %-7s  absent    -                              %s", s.name, s.what);
        } else if (!s.has_driver) {
            out.printf("  %-7s  no-drv    -                              %s", s.name, s.what);
        } else if (const Status st = s.sample(buf, sizeof buf); st == Status::Ok) {
            out.printf("  %-7s  ok        %-30s %s", s.name, buf, s.what);
        } else {
            out.printf("  %-7s  %-9s -                              %s", s.name, cmd::name(st),
                       s.what);
        }
    }
    out.line("  absent = not fitted on this board · no-drv = fitted, but no driver reads it yet");
    return Status::Ok;
}

Status cmd_read(Args const& a, Sink& out) {
    const SensorSpec* s = find_sensor(a.obj);
    if (!s) {
        out.printf("no such sensor '%s'  (`sensor list`)", a.obj ? a.obj : "");
        return Status::BadArg;
    }
    if (!board::present(s->dev)) {
        out.printf("%s: not present", s->name);
        return Status::NotPresent;
    }
    if (!s->has_driver) {
        out.printf("%s: no driver yet -- see FIRMWARE.md §12.1", s->name);
        return Status::NotReady;
    }
    char buf[96];
    const Status st = s->sample(buf, sizeof buf);
    if (st != Status::Ok) {
        out.printf("%s: %s", s->name, cmd::name(st));
        return st;
    }
    out.printf("%s  %s", s->name, buf);
    return Status::Ok;
}

Status cmd_stream(Args const& a, Sink& out) {
    const SensorSpec* s = find_sensor(a.obj);
    if (!s) {
        out.printf("no such sensor '%s'  (`sensor list`)", a.obj ? a.obj : "");
        return Status::BadArg;
    }
    if (!s->has_driver) {
        out.printf("%s: no driver yet -- see FIRMWARE.md §12.1", s->name);
        return Status::NotReady;
    }

    StreamOpts o;
    o.csv = a.has_flag("--csv");
    o.keep_logs = a.has_flag("--keep-logs");

    int n = 0;
    for (int i = 0; i < a.count(); ++i) {
        const char* v = a.arg(i);
        if (!v || v[0] == '-') continue;
        const long x = std::strtol(v, nullptr, 10);
        if (n == 0) o.hz = static_cast<uint16_t>(x);
        if (n == 1) o.secs = static_cast<uint32_t>(x);
        ++n;
    }
    if (o.hz > s->max_hz) {
        out.printf("%s tops out at %u Hz (asked %u) -- clamped", s->name, s->max_hz, o.hz);
        o.hz = s->max_hz;
    }
    return run_stream(s->name, s->sample, o, out);
}

Status cmd_stop(Args const&, Sink& out) {
    // Streams are bounded and synchronous today, so there is never one running while you
    // have a prompt.  Kept registered because §9.3 documents it and because it becomes real
    // the moment the owning AO produces samples asynchronously.
    out.line("no stream running (streams are bounded and hold the console until they end)");
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"sensor", nullptr, "list", "", "what exists, what is fitted, last value", ReleaseOk, cmd_list},
    {"sensor", kAnyObject, "read", "", "one sample", ReleaseOk, cmd_read},
    {"sensor", kAnyObject, "stream", "[<hz>] [<s>] [--csv] [--keep-logs]",
     "bounded stream, <=200 Hz, <=120 s", static_cast<uint16_t>(ReleaseOk | Streaming), cmd_stream},
    {"sensor", nullptr, "stop", "", "end a running stream", ReleaseOk, cmd_stop},
};

}  // namespace

extern const CmdTable kTableSensor{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
