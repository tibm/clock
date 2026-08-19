// The `motion` group.                                        [FIRMWARE.md §9.3, §6.1]
//
// A view onto the AO, never a second driver: every row here posts an event to `motion` and
// reads its snapshot.  Nothing in this file touches hal::motor -- one owner per peripheral
// (rule 1), and a CLI that could drive the coils behind the AO's back would be exactly the
// bug that rule exists to prevent.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clk/cli/registry.hpp"
#include "clk/domain/hand.hpp"
#include "clk/services/motion.hpp"

namespace clk::cli {
namespace {

using svc::Motion;

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

// "07:30" | "7:30" | "0730"
bool parse_hhmm(const char* s, int& h, int& m) {
    if (!s) return false;
    char* end = nullptr;
    const long a = std::strtol(s, &end, 10);
    if (!end) return false;
    if (*end == ':') {
        const long b = std::strtol(end + 1, nullptr, 10);
        h = static_cast<int>(a);
        m = static_cast<int>(b);
    } else if (*end == '\0' && std::strlen(s) == 4) {
        h = static_cast<int>(a / 100);
        m = static_cast<int>(a % 100);
    } else {
        return false;
    }
    return h >= 0 && h < 24 && m >= 0 && m < 60;
}

Status cmd_status(Args const&, Sink& out) {
    const auto s = svc::motion().snapshot();
    out.printf("state  %s%s%s%s", s.state_name, s.phase[0] ? " / " : "", s.phase,
               s.homed ? "  [homed]" : "  [not homed]");
    out.printf("hands  h=%" PRId32 " m=%" PRId32 "  (%.1f deg / %.1f deg)", s.hour, s.minute,
               static_cast<double>(domain::to_deg(s.hour)),
               static_cast<double>(domain::to_deg(s.minute)));
    out.printf("target h=%" PRId32 " m=%" PRId32, s.target_hour, s.target_minute);
    out.printf("coils  %s   opto %.3f   faults %" PRIu32, s.powered ? "live" : "off",
               static_cast<double>(s.opto), s.faults);
    out.printf("zero   h=%" PRId32 " m=%" PRId32 " usteps  (%+.2f / %+.2f deg, per unit)", s.zero_h,
               s.zero_m, static_cast<double>(s.zero_h) * 360.0 / domain::kRev,
               static_cast<double>(s.zero_m) * 360.0 / domain::kRev);
    out.printf("trims  %" PRIu32 "  last %+" PRId32 " usteps  (auto-home, on every crossing)",
               s.trims, s.last_trim);
    const auto t = svc::motion().tuning();
    out.printf("dial   tick %u/12 = %d deg, %+" PRId32 " usteps on every target%s", s.dial_tick,
               s.dial_tick * 30, s.dial_off, t.level ? "" : "   [levelling off]");
    if (s.home_ms) out.printf("last home took %" PRIu32 " ms of sim time", s.home_ms);
    out.printf("tune   v_max=%" PRId32 " accel=%" PRId32 " v_coarse=%" PRId32 " v_fine=%" PRId32
               " backlash=%" PRId32 " thresh=%.2f autohome=%d level=%d",
               t.v_max, t.accel, t.v_coarse, t.v_fine, t.backlash,
               static_cast<double>(t.opto_thresh), t.autohome ? 1 : 0, t.level ? 1 : 0);
    return Status::Ok;
}

Status cmd_home(Args const&, Sink& out) {
    svc::motion().home();
    out.line("homing: clear -> minute coarse+fine -> park -> hour coarse+fine");
    out.line("  watch it with `motion status` or the ux app");
    return Status::Ok;
}

Status cmd_goto(Args const& a, Sink& out) {
    int h = 0, m = 0;
    if (!parse_hhmm(a.arg(0), h, m)) {
        out.line("usage: motion goto <hh:mm>");
        return Status::BadArg;
    }
    const auto p = domain::for_time(h, m);
    svc::motion().goto_usteps(p.hour, p.minute);
    out.printf("goto %02d:%02d  (h=%" PRId32 " m=%" PRId32 " usteps)", h, m, p.hour, p.minute);
    return Status::Ok;
}

Status cmd_step(Args const& a, Sink& out) {
    hal::motor::Hand hand{};
    if (!parse_hand(a.arg(0), hand) || a.count() < 2) {
        out.line("usage: motion step <h|m> <+/-usteps>");
        return Status::BadArg;
    }
    const auto n = static_cast<int32_t>(std::strtol(a.arg(1), nullptr, 10));
    svc::motion().nudge(hand, n);
    out.printf("%s %+" PRId32 " usteps (%.2f deg)", a.arg(0), n,
               static_cast<double>(n) * 360.0 / domain::kRev);
    return Status::Ok;
}

Status cmd_stop(Args const&, Sink& out) {
    svc::motion().halt();
    out.line("stopped");
    return Status::Ok;
}

// The bench knob.  Everything here is a number you will want to change while watching the
// hands, which is exactly why it is a command and not a constant.
Status cmd_tune(Args const& a, Sink& out) {
    auto t = svc::motion().tuning();
    if (a.count() < 2) {
        out.line(
            "usage: motion tune "
            "<v_max|accel|v_coarse|v_fine|backlash|thresh|autohome|level> <value>");
        out.printf("  v_max=%" PRId32 " accel=%" PRId32 " v_coarse=%" PRId32 " v_fine=%" PRId32
                   " backlash=%" PRId32 " thresh=%.2f autohome=%d level=%d",
                   t.v_max, t.accel, t.v_coarse, t.v_fine, t.backlash,
                   static_cast<double>(t.opto_thresh), t.autohome ? 1 : 0, t.level ? 1 : 0);
        return a.count() == 0 ? Status::Ok : Status::BadArg;
    }
    const char* k = a.arg(0);
    const double v = std::strtod(a.arg(1), nullptr);
    const auto i = static_cast<int32_t>(v);
    if (std::strcmp(k, "v_max") == 0) {
        t.v_max = i;
    } else if (std::strcmp(k, "accel") == 0) {
        t.accel = i;
    } else if (std::strcmp(k, "v_coarse") == 0) {
        t.v_coarse = i;
    } else if (std::strcmp(k, "v_fine") == 0) {
        t.v_fine = i;
    } else if (std::strcmp(k, "backlash") == 0) {
        t.backlash = i;
    } else if (std::strcmp(k, "thresh") == 0) {
        t.opto_thresh = static_cast<float>(v);
    } else if (std::strcmp(k, "autohome") == 0) {
        t.autohome = i != 0;
    } else if (std::strcmp(k, "level") == 0) {
        // Off puts the dial back to the printed 12 straight away -- see Motion::set_tuning.
        t.level = i != 0;
    } else {
        out.printf("no such knob '%s'", k);
        return Status::BadArg;
    }
    svc::motion().set_tuning(t);
    out.printf("%s = %g", k, v);
    return Status::Ok;
}

// The per-unit calibration, and the only number in this file that is about ONE clock rather
// than about the design.  The opto says "the mark is over the window"; north is where the
// hand has to point for the dial to be right, and the gap between the two is a fact about how
// this movement was assembled.  Positive pushes the hand clockwise.  It lands in NVS, it is
// what homing adopts, and it is what the automatic trim measures against (§6.1).
Status cmd_zero(Args const& a, Sink& out) {
    const auto s = svc::motion().snapshot();
    if (a.count() == 0) {
        out.printf("zero h=%" PRId32 " m=%" PRId32 " usteps  (%+.2f / %+.2f deg)", s.zero_h,
                   s.zero_m, static_cast<double>(s.zero_h) * 360.0 / domain::kRev,
                   static_cast<double>(s.zero_m) * 360.0 / domain::kRev);
        out.line("  usage: motion zero <h|m> <+/-usteps>   (48 usteps = 1 deg, + = clockwise)");
        return Status::Ok;
    }
    hal::motor::Hand hand{};
    if (!parse_hand(a.arg(0), hand) || a.count() < 2) {
        out.line("usage: motion zero [<h|m> <+/-usteps>]");
        return Status::BadArg;
    }
    const auto n = static_cast<int32_t>(std::strtol(a.arg(1), nullptr, 10));
    // A trim is a trim: anything approaching a whole revolution is a typo, and applying it
    // would move the hand there rather than tell you so.
    if (n <= -domain::kRev / 4 || n >= domain::kRev / 4) {
        out.printf("out of range: %+" PRId32 " usteps is not a calibration, it is a move", n);
        return Status::BadArg;
    }
    svc::motion().set_zero(hand, n);
    out.printf("zero %s = %+" PRId32 " usteps (%+.2f deg)%s", a.arg(0), n,
               static_cast<double>(n) * 360.0 / domain::kRev,
               s.homed ? " -- the hand moves to it now" : " -- applied at the next home");
    return Status::Ok;
}

Status cmd_spr(Args const&, Sink& out) {
    // §13 open question 1: 1080 full steps x16 is the X27 base spec, deferred to by the X40
    // addendum and still unverified.  Everything downstream reads the constant.
    out.printf("steps_per_rev %" PRId32 " usteps (1080 full x16) -- UNVERIFIED, bench it",
               domain::kRev);
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"motion", nullptr, "status", "", "state, hands, coils, tuning", ReleaseOk, cmd_status},
    {"motion", nullptr, "home", "", "run the homing FSM", Unsafe, cmd_home},
    {"motion", nullptr, "goto", "<hh:mm>", "drive the hands to a time", Unsafe, cmd_goto},
    {"motion", nullptr, "step", "<h|m> <+/-n>", "relative microsteps, for bring-up", Unsafe,
     cmd_step},
    {"motion", nullptr, "stop", "", "stop where you are", None, cmd_stop},
    {"motion", nullptr, "tune", "[<knob> <value>]", "profile + homing parameters", None, cmd_tune},
    {"motion", nullptr, "zero", "[<h|m> <+/-usteps>]", "per-unit index trim (NVS)", None, cmd_zero},
    {"motion", nullptr, "spr", "", "microsteps per revolution", ReleaseOk, cmd_spr},
};

}  // namespace

extern const CmdTable kTableMotion{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
