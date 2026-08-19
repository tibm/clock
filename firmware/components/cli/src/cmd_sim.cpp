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
#include "clk/domain/level.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/log.hpp"

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

bool arg_on(Args const& a, int i, bool& out) {
    const char* v = a.arg(i);
    if (!v) return false;
    if (std::strcmp(v, "on") == 0 || std::strcmp(v, "1") == 0) {
        out = true;
        return true;
    }
    if (std::strcmp(v, "off") == 0 || std::strcmp(v, "0") == 0) {
        out = false;
        return true;
    }
    return false;
}

bool parse_hand(const char* s, hal::motor::Hand& out) {
    if (!s) return false;
    if (std::strcmp(s, "h") == 0 || std::strcmp(s, "hour") == 0) {
        out = hal::motor::Hand::Hour;
        return true;
    }
    if (std::strcmp(s, "m") == 0 || std::strcmp(s, "minute") == 0) {
        out = hal::motor::Hand::Minute;
        return true;
    }
    return false;
}

Status cmd_opto(Args const& a, Sink& out) {
    if (a.count() == 0) {
        out.printf("opto %.3f  (%s)", sim::opto(),
                   sim::opto_auto() ? "auto, from the hands" : "held");
        return Status::Ok;
    }
    if (std::strcmp(a.arg(0), "auto") == 0) {
        sim::set_opto_auto(true);
        out.printf("opto %.3f  -- derived from the hands again", sim::opto());
        return Status::Ok;
    }
    const double v = arg_d(a, 0, -1);
    if (v < 0.0 || v > 1.0) {
        out.line("usage: sim opto <0..1>|auto   (0 = dark, 1 = full reflection)");
        out.line("  a value HOLDS the sensor there; `auto` derives it from the hand angles");
        return Status::BadArg;
    }
    sim::set_opto(static_cast<float>(v));
    out.printf("opto %.3f  (held -- `sim opto auto` to follow the hands)", sim::opto());
    return Status::Ok;
}

// The hands as the MECHANISM has them, which is not what the firmware thinks: that gap is
// the whole reason homing exists.  Setting an angle is reaching in and moving a hand.
Status cmd_hand(Args const& a, Sink& out) {
    if (a.count() == 0) {
        for (auto h : {hal::motor::Hand::Hour, hal::motor::Hand::Minute}) {
            const auto ax = hal::motor::state(h);
            out.printf("%-6s %6.1f deg   pos=%" PRId32 " usteps  vel=%" PRId32 "  err=%+.1f deg",
                       h == hal::motor::Hand::Hour ? "hour" : "minute",
                       static_cast<double>(sim::hand_angle(h)), ax.pos, ax.vel,
                       static_cast<double>(sim::hand_offset(h)));
        }
        out.printf("motor %s   (err is what `motion home` has to discover)",
                   hal::motor::enabled() ? "energized" : "standby");
        return Status::Ok;
    }
    hal::motor::Hand h{};
    if (!parse_hand(a.arg(0), h)) {
        out.line("usage: sim hand [<h|m> <deg>]   (0 = 12 o'clock, clockwise)");
        return Status::BadArg;
    }
    if (a.count() < 2) {
        out.printf("%s %.1f deg", a.arg(0), static_cast<double>(sim::hand_angle(h)));
        return Status::Ok;
    }
    sim::set_hand_angle(h, static_cast<float>(arg_d(a, 1, 0.0)));
    out.printf("%s now points at %.1f deg  (opto %.3f)", a.arg(0),
               static_cast<double>(sim::hand_angle(h)), sim::opto());
    return Status::Ok;
}

Status cmd_motor(Args const& a, Sink& out) {
    bool on = false;
    if (!arg_on(a, 0, on)) {
        out.line("usage: sim motor <on|off>   (STEP_STBY; off freezes the hands)");
        return Status::BadArg;
    }
    const Status st = hal::motor::enable(on);
    if (st != Status::Ok) {
        out.printf("motor %s", cmd::name(st));
        return st;
    }
    out.printf("STEP_STBY %s", on ? "high (coils live)" : "low (coils dead, hands frozen)");
    return Status::Ok;
}

// How the cube is sitting.  `yaw` turns it about the dial's own axis -- the plate the ux app
// draws -- and every thirty degrees of it is one tick of §6.1d; `pitch` tips it away from
// vertical, and at +/-90 the dial faces the ceiling and gravity has nothing to say about
// which way up it is, which is the case the dead zone exists for.  Both come back out as a
// gravity vector, because that is all the firmware ever sees.
Status cmd_imu(Args const& a, Sink& out) {
    if (a.count() == 0) {
        const auto s = hal::imu::read();
        if (!s.ok()) {
            out.printf("imu %s", cmd::name(s.st));
            return s.st;
        }
        out.printf("yaw %.1f  pitch %.1f  roll %.1f  taps %u", static_cast<double>(s.v.yaw_deg),
                   static_cast<double>(s.v.pitch_deg), static_cast<double>(s.v.roll_deg), s.v.taps);
        out.printf("gravity %.2f, %.2f, %.2f m/s2 (dial axes)  ->  up is %.0f deg round the dial",
                   static_cast<double>(s.v.gx), static_cast<double>(s.v.gy),
                   static_cast<double>(s.v.gz),
                   static_cast<double>(domain::up_deg(s.v.gx, s.v.gy)));
        return Status::Ok;
    }
    sim::set_orientation(static_cast<float>(arg_d(a, 0, 0.0)), static_cast<float>(arg_d(a, 1, 0.0)),
                         static_cast<float>(arg_d(a, 2, 0.0)));
    out.printf("yaw %.1f deg  pitch %.1f deg", arg_d(a, 0, 0.0), arg_d(a, 1, 0.0));
    return Status::Ok;
}

Status cmd_tap(Args const&, Sink& out) {
    sim::tap();
    out.line("top-tap (BNO085 tap counter +1)");
    return Status::Ok;
}

Status cmd_radio(Args const& a, Sink& out) {
    bool off = false;
    if (!arg_on(a, 0, off)) {
        out.line("usage: sim radio <on|off>   (the rear J11 toggle; on = radios DISABLED)");
        return Status::BadArg;
    }
    // Polarity is the hardware's: the pin idles high through the expander pull-up and the
    // toggle pulls it low, so a broken harness fails to radios-enabled (README §16d).
    sim::set_expander_in(hal::expander::Sig::RadioOff, !off);
    out.printf("RADIO_OFF %s -- radios %s", off ? "low (asserted)" : "high (open)",
               off ? "disabled" : "enabled");
    return Status::Ok;
}

Status cmd_speaker(Args const& a, Sink& out) {
    bool on = false;
    if (!arg_on(a, 0, on)) {
        out.line("usage: sim speaker <on|off>   (stands in for the `audio` AO)");
        return Status::BadArg;
    }
    sim::set_speaker(on);
    out.printf("speaker %s  vol %u%%", on ? "on" : "off", hal::audio::volume_pct());
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
    // `down`/`up` are the edge form the UI sends -- a mouse button held over the knob is a
    // press whose length nobody knows in advance, which is exactly a long-press.
    if (w && (std::strcmp(w, "hold") == 0 || std::strcmp(w, "down") == 0))
        ms = kHoldForever;
    else if (w && (std::strcmp(w, "release") == 0 || std::strcmp(w, "up") == 0))
        ms = 0;
    else if (w)
        ms = static_cast<uint32_t>(std::strtoul(w, nullptr, 10));
    sim::press(ms);
    if (ms == 0)
        out.line("ENC_SW released");
    else if (ms == kHoldForever)
        out.line("ENC_SW held (until `sim press up`)");
    else
        out.printf("ENC_SW pressed for %" PRIu32 " ms of sim time", ms);
    return Status::Ok;
}

// Raw PCNT counts rather than detents: a knob being dragged in the UI produces a continuous
// angle, and 256 counts/rev is fine enough that rounding it to detents would be visible.
//
// `over <ms>` is the same counts delivered at a RATE, which is the only way to say "a spin"
// rather than "a teleport".  It matters because the ui paces a setting to what the hands can
// draw (§6.6d): forty counts inside one 20 ms poll is a turn no finger performed, and the
// firmware is right to spend one minute of it and drop the rest.  Spread the same forty over
// a second and every one of them lands.
Status cmd_knob(Args const& a, Sink& out) {
    const long c = arg_l(a, 0, 0);
    if (c == 0) {
        out.line("usage: sim knob <+/-counts> [over <ms>]   (256 counts/rev; `sim turn` too)");
        return Status::BadArg;
    }
    long ms = 0;
    if (a.count() >= 2) {
        const char* w = a.arg(1);
        const bool worded = std::strcmp(w, "over") == 0;
        if (worded && a.count() < 3) {
            out.line("usage: sim knob <+/-counts> over <ms>");
            return Status::BadArg;
        }
        ms = std::strtol(worded ? a.arg(2) : w, nullptr, 10);
        if (ms < 0) return Status::BadArg;
    }
    sim::turn_counts_over(static_cast<int32_t>(c), static_cast<uint32_t>(ms));
    const auto k = hal::knob::read();
    if (k.ok())
        out.printf("knob count=%" PRId32 " (%+ld counts = %+.1f deg%s%ld%s)", k.v.count, c,
                   static_cast<double>(c) * 360.0 / 256.0, ms ? ", over " : "", ms ? ms : 0,
                   ms ? " ms" : "");
    else
        out.printf("knob %s", cmd::name(k.st));
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

// The fake HARDWARE only.  The services keep running and keep believing whatever they
// believed -- motion still thinks it is homed while the hands have jumped -- which is the
// interesting half of the pair.  `sys reboot` is the other half, and it is not host-only:
// it restarts the image here and on the board.
Status cmd_reset(Args const&, Sink& out) {
    sim::reset();
    out.line("fake hardware back to power-on state");
    out.line("  the SERVICES are untouched -- motion still believes whatever it believed.");
    out.line("  `sys reboot` for a cold start of the image itself.");
    return Status::Ok;
}

Status cmd_status(Args const&, Sink& out) {
    char px[16];
    sim::render_pixels(px, sizeof px);
    const auto s = sim::snapshot();
    out.printf("time   sim_ms=%" PRIu32 "  warp=%.2fx", hal::clock_::millis(), s.warp);
    out.printf("hands  h=%.1f deg m=%.1f deg  pos=%" PRId32 "/%" PRId32 "  motor=%s%s",
               static_cast<double>(s.hand_deg[0]), static_cast<double>(s.hand_deg[1]),
               s.hand_pos[0], s.hand_pos[1], s.motor_on ? "on" : "off",
               (s.hand_moving[0] || s.hand_moving[1]) ? "  MOVING" : "");
    out.printf("opto   %.3f  (%s)", static_cast<double>(s.opto), s.opto_auto ? "auto" : "held");
    out.printf("power  %u mV  soc %u%%  plugged=%d  chrg=%d", s.vbat_mv, s.soc_pct,
               s.plugged ? 1 : 0, s.charging ? 1 : 0);
    out.printf("knob   count=%" PRId32 " sw=%d", s.knob_count, s.knob_sw ? 1 : 0);
    out.printf("imu    yaw=%.1f taps=%u", static_cast<double>(s.yaw_deg), s.taps);
    out.printf("pixels [%s]  refreshed=%d", px, s.refreshed ? 1 : 0);
    out.printf("wake   warm=%u%% cool=%u%%", s.warm_pct, s.cool_pct);
    out.printf("sound  speaker=%s vol=%u%%", s.spk_active ? "on" : "off", s.vol_pct);
    out.printf("radio  %s", s.radio_off ? "OFF (rear toggle asserted)" : "enabled");
    return Status::Ok;
}

constexpr uint16_t kHost = static_cast<uint16_t>(HostOnly);

constexpr CmdSpec kRows[] = {
    {"sim", nullptr, "status", "", "everything the fakes currently hold", kHost, cmd_status},
    {"sim", nullptr, "opto", "[<0..1>|auto]", "homing reflectance", kHost, cmd_opto},
    {"sim", nullptr, "hand", "[<h|m> <deg>]", "where the hands physically are", kHost, cmd_hand},
    {"sim", nullptr, "motor", "<on|off>", "STEP_STBY -- off freezes the hands", kHost, cmd_motor},
    {"sim", nullptr, "imu", "[<yaw> [<pitch> <roll>]]", "how the cube sits -> gravity", kHost,
     cmd_imu},
    {"sim", nullptr, "tap", "", "one top-tap (tap-to-snooze)", kHost, cmd_tap},
    {"sim", nullptr, "radio", "<on|off>", "rear J11 toggle; on = radios off", kHost, cmd_radio},
    {"sim", nullptr, "speaker", "<on|off>", "amp out of shutdown", kHost, cmd_speaker},
    {"sim", nullptr, "vbat", "<mV>", "cell voltage", kHost, cmd_vbat},
    {"sim", nullptr, "noise", "<mV>", "ADC noise, deterministic", kHost, cmd_noise},
    {"sim", nullptr, "seed", "<n>", "reseed the noise PRNG", kHost, cmd_seed},
    {"sim", nullptr, "turn", "<+/-detents>", "rotate the knob", kHost, cmd_turn},
    {"sim", nullptr, "knob", "<+/-counts> [over <ms>]", "rotate the knob, raw PCNT counts", kHost,
     cmd_knob},
    {"sim", nullptr, "press", "[<ms>|down|up]", "press ENC_SW", kHost, cmd_press},
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
