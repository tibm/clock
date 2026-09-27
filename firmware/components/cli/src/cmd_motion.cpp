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

// A refusal must never print as a success -- FIRMWARE.md §12.2 ground rule 2, and F0.1 is the
// instance that cost a bench session: `motion step` posted a target, the FSM dropped it in
// `Fault`, and the command printed `m +100 usteps (2.08 deg)` as though a hand had moved.
//
// The second line is the part that matters.  On a bring-up board there are TWO gates that stop
// a move and they are indistinguishable from the outside: the NVS bench inhibit, which
// `unsafe on` does NOT lift, and a `Fault` the homing FSM never left (F0.2).  So every row that
// asks the movement for something routes its non-Ok answer through here, and the answer names
// the gate and the cure.
Status refused(Sink& out, const char* what, Status st) {
    out.printf("%s refused: %s", what, cmd::name(st));
    switch (st) {
        case Status::Denied:
            out.line("  the movement is INHIBITED -- `motion power on` releases it (saved to NVS)");
            out.line("  `unsafe on` does NOT lift this; it is a different gate (board.hpp)");
            break;
        case Status::NotReady:
            out.line("  homing failed and nothing has cleared it -- `motion stop` clears a fault");
            out.line("  `motion step` still works in a fault: it is how the index mark is placed");
            break;
        case Status::Busy:
            out.line("  a homing run has both shafts -- `motion stop` abandons it");
            break;
        case Status::NotPresent:
            out.line("  no movement fitted -- board.hpp's presence mask says so");
            break;
        default:
            out.line("  the motion mailbox was full and the event was dropped (`sys stat`)");
            break;
    }
    return st;
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
    if (const Status st = svc::motion().home(); st != Status::Ok) return refused(out, "home", st);
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
    if (const Status st = svc::motion().goto_usteps(p.hour, p.minute); st != Status::Ok) {
        return refused(out, "goto", st);
    }
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
    if (const Status st = svc::motion().nudge(hand, n); st != Status::Ok) {
        return refused(out, "step", st);
    }
    out.printf("%s %+" PRId32 " usteps (%.2f deg)", a.arg(0), n,
               static_cast<double>(n) * 360.0 / domain::kRev);
    return Status::Ok;
}

// Also the way out of a `Fault` -- see Motion::on_event's Halt handler.  Saying so here rather
// than only in the help line: on a board whose index mark is not placed, this is the command
// that makes the movement usable again, and it is not the one anybody would guess.
Status cmd_stop(Args const&, Sink& out) {
    const bool was_faulted = svc::motion().snapshot().state == Motion::State::Fault;
    if (const Status st = svc::motion().halt(); st != Status::Ok) return refused(out, "stop", st);
    if (was_faulted) {
        out.line("stopped -- and the fault is cleared (still not homed)");
        out.line("  `motion step` to place the index mark, `motion home` when it is there");
    } else {
        out.line("stopped");
    }
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
        out.line("  usage: motion zero <h|m> <+/-usteps>   (32 usteps = 1 deg, + = clockwise)");
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
    // §13 open question 1: X27 spec gear 1:180 = 180 electrical periods x 64 usteps.  Still
    // unverified on a stopless movement.  Everything downstream reads the constant.
    out.printf("steps_per_rev %" PRId32 " usteps (180 periods x64) -- UNVERIFIED, bench it",
               domain::kRev);
    return Status::Ok;
}

// The bench switch for the movement (hal.hpp).  ⚠ Unsafe, and it is the one command here
// whose UNSAFE direction is "on": releasing the inhibit means the next `motion home` drives
// both hands, on a mechanism that may be half-assembled.
Status cmd_power(Args const& a, Sink& out) {
    const char* v = a.arg(0);
    if (!v) {
        out.printf("movement %s%s", hal::motor::inhibited() ? "INHIBITED" : "released",
                   hal::motor::enabled() ? ", coils live" : ", coils off");
        if (hal::motor::inhibited()) out.line("  `motion power on` releases it (saved to NVS)");
        return Status::Ok;
    }
    const bool on = std::strcmp(v, "on") == 0;
    if (!on && std::strcmp(v, "off") != 0) {
        out.line("usage: motion power [on|off]");
        return Status::BadArg;
    }
    const Status st = hal::motor::inhibit(!on);
    out.printf("movement %s%s", on ? "released" : "INHIBITED",
               st == Status::Ok ? " (saved)" : " (NOT saved -- NVS refused)");
    return st;
}

// ---- raw coil bench (2026-09-27) ----------------------------------------------------------
// The one exception to "nothing here touches hal::motor": these exist to test the bridges and
// the movement with the commutator OUT of the loop, so going through `motion` would defeat
// them.  They refuse while the AO is driving, and the AO re-syncs its power flag from
// hal::motor::enabled() on its next move, so nothing is left inconsistent.
Status bench_coils_up(Sink& out, const char* what) {
    const auto s = svc::motion().snapshot();
    if (s.state == Motion::State::Homing || s.state == Motion::State::Moving) {
        return refused(out, what, Status::Busy);
    }
    if (const Status st = hal::motor::enable(true); st != Status::Ok) return refused(out, what, st);
    return Status::Ok;
}

Status cmd_coil(Args const& a, Sink& out) {
    hal::motor::Hand hand{};
    if (!parse_hand(a.arg(0), hand) || a.count() < 3) {
        out.line(
            "usage: motion coil <h|m> <A permille> <B permille>   (-1000..1000, 1000 = 5 V DC)");
        out.line("  h = tube (TB6612 #1, IO3-6)   m = inner pin (TB6612 #2, IO38-41)");
        out.line("  `motion coil h 0 0` + `motion power off` or `motion stop` when done");
        return Status::BadArg;
    }
    const auto pa = static_cast<int16_t>(std::strtol(a.arg(1), nullptr, 10));
    const auto pb = static_cast<int16_t>(std::strtol(a.arg(2), nullptr, 10));
    if (const Status st = bench_coils_up(out, "coil"); st != Status::Ok) return st;
    if (const Status st = hal::motor::coils(hand, pa, pb); st != Status::Ok) {
        out.printf("coil refused: %s  (range -1000..1000)", cmd::name(st));
        return st;
    }
    out.printf("%s  A=%+d  B=%+d permille  (~%+.2f V / %+.2f V across the coils, %s decay)",
               a.arg(0), pa, pb, pa * 5.0 / 1000, pb * 5.0 / 1000,
               hal::motor::decay() == hal::motor::Decay::Slow ? "slow" : "fast");
    return Status::Ok;
}

Status cmd_decay(Args const& a, Sink& out) {
    const char* v = a.arg(0);
    if (v && std::strcmp(v, "slow") == 0) {
        hal::motor::set_decay(hal::motor::Decay::Slow);
    } else if (v && std::strcmp(v, "fast") == 0) {
        hal::motor::set_decay(hal::motor::Decay::Fast);
    } else if (v) {
        out.line("usage: motion decay [slow|fast]");
        return Status::BadArg;
    }
    out.printf("decay %s  (slow = drive/short-brake, linear; fast = drive/off, pre-2026-09-27)",
               hal::motor::decay() == hal::motor::Decay::Slow ? "slow" : "fast");
    return Status::Ok;
}

// The X27 spec's own partial-step sequence (SP-X27-e-C fig. 8), full 5 V, no PWM involved:
// coil 1 = + + 0 - - 0, coil 2 = + 0 - - 0 +.  Six states = one rotor turn = 2 deg of shaft,
// so 1080 states is one revolution.  If this does not turn a bare shaft, the firmware's
// waveform is not the problem.
Status cmd_walk(Args const& a, Sink& out) {
    hal::motor::Hand hand{};
    if (!parse_hand(a.arg(0), hand) || a.count() < 2) {
        out.line("usage: motion walk <h|m> <+/-states> [ms per state, default 3]");
        out.line("  6 states = 2 deg; 1080 = one turn; + = the datasheet's clockwise");
        return Status::BadArg;
    }
    const auto n = static_cast<int32_t>(std::strtol(a.arg(1), nullptr, 10));
    const auto ms = a.count() > 2 ? static_cast<int32_t>(std::strtol(a.arg(2), nullptr, 10)) : 3;
    const int32_t steps = n < 0 ? -n : n;
    if (ms < 1 || ms > 1000 || static_cast<int64_t>(steps) * ms > 20'000) {
        out.line("walk: 1..1000 ms per state, and at most 20 s in total");
        return Status::BadArg;
    }
    if (const Status st = bench_coils_up(out, "walk"); st != Status::Ok) return st;
    static constexpr int8_t kC1[6] = {1, 1, 0, -1, -1, 0};
    static constexpr int8_t kC2[6] = {1, 0, -1, -1, 0, 1};
    int k = 0;
    for (int32_t i = 0; i <= steps; ++i) {  // state 0 first, then `steps` transitions
        const Status st = hal::motor::coils(hand, static_cast<int16_t>(kC1[k] * 1000),
                                            static_cast<int16_t>(kC2[k] * 1000));
        if (st != Status::Ok) return refused(out, "walk", st);
        hal::clock_::sleep_ms(static_cast<uint32_t>(ms));
        k = n >= 0 ? (k + 1) % 6 : (k + 5) % 6;
    }
    out.printf("walked %s %+" PRId32 " states (%.1f deg if nothing slipped), coils holding",
               a.arg(0), n, static_cast<double>(n) / 3.0);
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"motion", nullptr, "coil", "<h|m> <A> <B>", "raw coil duty, permille (bench)", Unsafe,
     cmd_coil},
    {"motion", nullptr, "decay", "[slow|fast]", "PWM off-time mode (bench)", None, cmd_decay},
    {"motion", nullptr, "walk", "<h|m> <+/-n> [ms]", "datasheet full-V partial steps (bench)",
     Unsafe, cmd_walk},
    {"motion", nullptr, "power", "[on|off]", "release or inhibit the movement (NVS)", Unsafe,
     cmd_power},
    {"motion", nullptr, "status", "", "state, hands, coils, tuning", ReleaseOk, cmd_status},
    {"motion", nullptr, "home", "", "run the homing FSM", Unsafe, cmd_home},
    {"motion", nullptr, "goto", "<hh:mm>", "drive the hands to a time", Unsafe, cmd_goto},
    {"motion", nullptr, "step", "<h|m> <+/-n>", "relative microsteps, for bring-up", Unsafe,
     cmd_step},
    {"motion", nullptr, "stop", "", "stop where you are; clears a fault", None, cmd_stop},
    {"motion", nullptr, "tune", "[<knob> <value>]", "profile + homing parameters", None, cmd_tune},
    {"motion", nullptr, "zero", "[<h|m> <+/-usteps>]", "per-unit index trim (NVS)", None, cmd_zero},
    {"motion", nullptr, "spr", "", "microsteps per revolution", ReleaseOk, cmd_spr},
};

}  // namespace

extern const CmdTable kTableMotion{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
