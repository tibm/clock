// The `sensor` group -- read-only, always safe, present in release builds.
// [FIRMWARE.md §9.5]
//
// This is a VIEW, not a driver: every sample goes through hal/, and when the AOs land it
// will go through the owning AO instead.  Nothing here touches a peripheral register.
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clk/board.hpp"
#include "clk/cli/registry.hpp"
#include "clk/cli/stream.hpp"
#include "clk/domain/level.hpp"
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

Status s_hands(char* out, std::size_t cap) {
    // The mechanism's view, not the firmware's: `pos` is what was commanded, and on real
    // hardware there is no way to read the other one.  In clocksim there is, which is the
    // single most useful thing about clocksim while tuning the homing sweep.
    const auto h = hal::motor::state(hal::motor::Hand::Hour);
    const auto m = hal::motor::state(hal::motor::Hand::Minute);
    std::snprintf(out, cap, "h=%" PRId32 " m=%" PRId32 " v=%" PRId32 "/%" PRId32 " mov=%d%d", h.pos,
                  m.pos, h.vel, m.vel, h.moving ? 1 : 0, m.moving ? 1 : 0);
    return Status::Ok;
}

Status s_imu(char* out, std::size_t cap) {
    const auto s = hal::imu::read();
    if (!s.ok()) return s.st;
    // Gravity first: it is the reading the product uses (§6.1d), and `up` is the one number
    // you actually want while turning a cube over on the bench -- where the top of the dial
    // has got to, in the dial's own frame.  The Euler angles are the bench's, not the code's.
    const double plane =
        std::sqrt(static_cast<double>(s.v.gx) * s.v.gx + static_cast<double>(s.v.gy) * s.v.gy);
    const double mag = std::sqrt(plane * plane + static_cast<double>(s.v.gz) * s.v.gz);
    std::snprintf(out, cap, "g=%.2f,%.2f,%.2f up=%.0f tilt=%.2f yaw=%.1f taps=%u",
                  static_cast<double>(s.v.gx), static_cast<double>(s.v.gy),
                  static_cast<double>(s.v.gz), static_cast<double>(domain::up_deg(s.v.gx, s.v.gy)),
                  mag > 0.0 ? plane / mag : 0.0, static_cast<double>(s.v.yaw_deg), s.v.taps);
    return Status::Ok;
}

Status s_exp(char* out, std::size_t cap) {
    // Both ports as bit strings in esp32.md's own order, plus the two signals a human is
    // actually watching.  Twelve `name=level` pairs would be 120 characters and would not
    // survive a 96-byte stream sample; the full decode belongs in `board exp` (§9.3).
    char bits[hal::expander::kSigCount + 1] = {};
    for (std::size_t i = 0; i < hal::expander::kSigCount; ++i) {
        const auto v = hal::expander::get(static_cast<hal::expander::Sig>(i));
        if (!v.ok()) return v.st;
        bits[i] = v.v ? '1' : '0';
    }
    std::snprintf(out, cap, "gpa=%.4s gpb=%.8s radio=%s stby=%d", bits, bits + 4,
                  bits[static_cast<int>(hal::expander::Sig::RadioOff)] == '0' ? "OFF" : "on",
                  bits[static_cast<int>(hal::expander::Sig::StepStby)] == '1' ? 1 : 0);
    return Status::Ok;
}

Status s_clk(char* out, std::size_t cap) {
    // `src` is asked of the silicon, not of the presence flag.  Reporting the flag made this
    // line say XTAL32K on any board built with a crystal footprint -- including one whose
    // crystal never started and whose RTC has been on the internal RC since boot, which is
    // the single fact this row exists to carry (kicad/REVIEW.md #24, §7.1).  When the two
    // disagree the row says so: the part is fitted and it is not oscillating.
    const auto src = hal::clock_::slow_src();
    const bool fell_back =
        board::present(board::Dev::Xtal32k) && src != hal::clock_::SlowSrc::Xtal32k;
    std::snprintf(out, cap, "sim_ms=%" PRIu32 " src=%s%s", hal::clock_::millis(),
                  hal::clock_::name(src), fell_back ? " (XTAL32K FITTED, NOT RUNNING)" : "");
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
    {"hands", board::Dev::Motor, 100, true, "microstep position + velocity", s_hands},
    {"vbat", board::Dev::Vbat, 10, true, "cell mV, SoC, charger state", s_vbat},
    {"clk", board::Dev::Xtal32k, 1, true, "slow-clock source, sim time", s_clk},
    {"imu", board::Dev::Imu, 20, true, "BNO085 gravity + taps", s_imu},
    {"exp", board::Dev::Expander, 20, true, "MCP23017 ports", s_exp},
    {"als", board::Dev::Als, 10, false, "TSL2591 lux", s_needs_driver},
    {"env", board::Dev::Env, 1, false, "BME688 T/RH/P/IAQ", s_needs_driver},
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
    // Three states can print here and the legend used to name two, leaving the one you
    // actually see all through bring-up -- the driver asked and got NotPresent -- unexplained.
    out.line("  absent = not fitted on this board · no-drv = fitted, but no driver reads it yet");
    out.line("  not-present = a driver asked and the device did not answer");
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
