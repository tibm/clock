// The `sim` group -- drives the fake hardware.             [FIRMWARE.md §9.3, §11.2]
//
// HOST ONLY.  On target these become event injection into the owning AO (`sim tap`,
// `sim alarm`, …) and arrive with the AOs; the rows that poke a fake peripheral
// (`sim opto`, `sim warp`) are marked HostOnly in §9.3 and stay here forever.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clk/board.hpp"
#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"

namespace clk::cli {
namespace {

namespace sim = hal::host;

constexpr uint32_t kHoldForever = 1000u * 60 * 60 * 24;  // "until released"

double arg_d(Args const& a, int i, double dflt) {
    const char* p = a.arg(i);
    return p ? std::strtod(p, nullptr) : dflt;
}
long arg_l(Args const& a, int i, long dflt) {
    const char* p = a.arg(i);
    return p ? std::strtol(p, nullptr, 10) : dflt;
}

Status cmd_opto(Args const& a, Sink& out) {
    if (a.count() == 0) {
        out.printf("opto %.3f", sim::opto());
        return Status::Ok;
    }
    const double v = arg_d(a, 0, -1);
    if (v < 0.0 || v > 1.0) {
        out.line("usage: sim opto <0..1>   (0 = dark, 1 = full reflection)");
        return Status::BadArg;
    }
    sim::set_opto(static_cast<float>(v));
    out.printf("opto %.3f", sim::opto());
    return Status::Ok;
}

Status cmd_vbat(Args const& a, Sink& out) {
    const long mv = arg_l(a, 0, -1);
    if (mv < 2500 || mv > 4400) {
        out.line("usage: sim vbat <mV>   (2500-4400)");
        return Status::BadArg;
    }
    sim::set_vbat_mv(static_cast<uint16_t>(mv));
    const auto p = hal::power::read();
    if (p.ok())
        out.printf("vbat %u mV  soc %u%%  chrg=%d", p.v.vbat_mv, p.v.soc_pct, p.v.charging ? 1 : 0);
    return Status::Ok;
}

Status cmd_noise(Args const& a, Sink& out) {
    const long mv = arg_l(a, 0, -1);
    if (mv < 0 || mv > 500) {
        out.line("usage: sim noise <mV peak>   (0-500, 0 = off)");
        return Status::BadArg;
    }
    sim::set_noise_mv(static_cast<uint16_t>(mv));
    out.printf("adc noise +/-%ld mV (deterministic; `sim seed` to change the sequence)", mv);
    return Status::Ok;
}

Status cmd_seed(Args const& a, Sink& out) {
    const long s = arg_l(a, 0, 1);
    sim::set_seed(static_cast<uint32_t>(s));
    out.printf("prng seed %ld -- noise is reproducible from here", s);
    return Status::Ok;
}

Status cmd_turn(Args const& a, Sink& out) {
    const long d = arg_l(a, 0, 0);
    if (d == 0) {
        out.line("usage: sim turn <+/-detents>   (4 PCNT counts each)");
        return Status::BadArg;
    }
    sim::turn(static_cast<int32_t>(d));
    const auto k = hal::knob::read();
    if (k.ok())
        out.printf("knob count=%" PRId32 " (%+ld detents)", k.v.count, d);
    else
        out.printf("knob %s", cmd::name(k.st));
    return Status::Ok;
}

Status cmd_press(Args const& a, Sink& out) {
    const char* w = a.arg(0);
    uint32_t ms = 200;  // a realistic human press
    if (w && std::strcmp(w, "hold") == 0)
        ms = kHoldForever;
    else if (w && std::strcmp(w, "release") == 0)
        ms = 0;
    else if (w)
        ms = static_cast<uint32_t>(std::strtoul(w, nullptr, 10));
    sim::press(ms);
    if (ms == 0)
        out.line("ENC_SW released");
    else if (ms == kHoldForever)
        out.line("ENC_SW held (until `sim press release`)");
    else
        out.printf("ENC_SW pressed for %" PRIu32 " ms of sim time", ms);
    return Status::Ok;
}

Status cmd_plug(Args const& a, Sink& out) {
    const bool on = std::strcmp(a.argv[a.first - 1], "plug") == 0;
    sim::set_plugged(on);
    out.printf("PD_PG %s", on ? "high (plugged)" : "low (on battery)");
    if (!on) out.line("  note: the wake light is now gated off (12 V boost is plugged-only)");
    return Status::Ok;
}

Status cmd_warp(Args const& a, Sink& out) {
    if (a.count() == 0) {
        out.printf("warp %.2fx", sim::warp());
        return Status::Ok;
    }
    const double x = arg_d(a, 0, -1);
    if (x <= 0) {
        out.line("usage: sim warp <factor>   (1 = real time, 60 = a minute per second)");
        return Status::BadArg;
    }
    sim::set_warp(x);
    out.printf("warp %.2fx   sim_ms=%" PRIu32, sim::warp(), hal::clock_::millis());
    return Status::Ok;
}

Status cmd_jump(Args const& a, Sink& out) {
    const double s = arg_d(a, 0, 0);
    if (s <= 0) {
        out.line("usage: sim jump <seconds>   (instant, does not run anything)");
        return Status::BadArg;
    }
    sim::advance(static_cast<uint64_t>(s * 1e6));
    out.printf("sim_ms=%" PRIu32, hal::clock_::millis());
    return Status::Ok;
}

Status cmd_present(Args const& a, Sink& out) {
    if (a.count() == 0) {
        char buf[200] = "";
        for (int i = 0; i < board::kDevCount; ++i) {
            const auto d = static_cast<board::Dev>(i);
            char one[32];
            std::snprintf(one, sizeof one, "%s%s ", board::name(d), board::present(d) ? "+" : "-");
            std::strncat(buf, one, sizeof buf - std::strlen(buf) - 1);
        }
        out.printf("%s", buf);
        out.line("  '+' fitted, '-' absent.  sim present <dev> <on|off>");
        return Status::Ok;
    }
    board::Dev d{};
    if (!board::parse_dev(a.arg(0), d)) {
        out.printf("no such device '%s'", a.arg(0));
        return Status::BadArg;
    }
    const char* v = a.arg(1);
    if (!v) {
        out.printf("%s %s", board::name(d), board::present(d) ? "fitted" : "absent");
        return Status::Ok;
    }
    const bool on = (std::strcmp(v, "on") == 0 || std::strcmp(v, "1") == 0);
    board::set_present(d, on);
    out.printf("%s %s", board::name(d), on ? "fitted" : "absent");
    return Status::Ok;
}

Status cmd_reset(Args const&, Sink& out) {
    sim::reset();
    out.line("fake hardware back to power-on state");
    return Status::Ok;
}

Status cmd_status(Args const&, Sink& out) {
    char px[16];
    sim::render_pixels(px, sizeof px);
    const auto p = hal::power::read();
    out.printf("time   sim_ms=%" PRIu32 "  warp=%.2fx", hal::clock_::millis(), sim::warp());
    out.printf("opto   %.3f", sim::opto());
    if (p.ok())
        out.printf("power  %u mV  soc %u%%  plugged=%d  chrg=%d", p.v.vbat_mv, p.v.soc_pct,
                   p.v.plugged ? 1 : 0, p.v.charging ? 1 : 0);
    const auto k = hal::knob::read();
    if (k.ok()) out.printf("knob   count=%" PRId32 " sw=%d", k.v.count, k.v.sw ? 1 : 0);
    out.printf("pixels [%s]  refreshed=%d", px, sim::refreshed() ? 1 : 0);
    out.printf("wake   warm=%u%% cool=%u%%", hal::wake::warm(), hal::wake::cool());
    return Status::Ok;
}

constexpr uint16_t kHost = static_cast<uint16_t>(HostOnly);

constexpr CmdSpec kRows[] = {
    {"sim", nullptr, "status", "", "everything the fakes currently hold", kHost, cmd_status},
    {"sim", nullptr, "opto", "[<0..1>]", "homing reflectance", kHost, cmd_opto},
    {"sim", nullptr, "vbat", "<mV>", "cell voltage", kHost, cmd_vbat},
    {"sim", nullptr, "noise", "<mV>", "ADC noise, deterministic", kHost, cmd_noise},
    {"sim", nullptr, "seed", "<n>", "reseed the noise PRNG", kHost, cmd_seed},
    {"sim", nullptr, "turn", "<+/-detents>", "rotate the knob", kHost, cmd_turn},
    {"sim", nullptr, "press", "[<ms>|hold|release]", "press ENC_SW", kHost, cmd_press},
    {"sim", nullptr, "plug", "", "PD_PG high", kHost, cmd_plug},
    {"sim", nullptr, "unplug", "", "PD_PG low -- wake light gates off", kHost, cmd_plug},
    {"sim", nullptr, "warp", "[<factor>]", "scale sim time against wall time", kHost, cmd_warp},
    {"sim", nullptr, "jump", "<seconds>", "advance sim time instantly", kHost, cmd_jump},
    {"sim", nullptr, "present", "[<dev> [on|off]]", "fit or unfit a device", kHost, cmd_present},
    {"sim", nullptr, "reset", "", "fakes back to power-on", kHost, cmd_reset},
};

}  // namespace

extern const CmdTable kTableSim{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
