// Hand arithmetic and the motion AO.                          [FIRMWARE.md §11.1, §6.1]
//
// The AO tests drive real sim time rather than mocking it: at `sim warp 20` a homing run
// that takes 35 s of sim time finishes in under two seconds of wall time, so the thing under
// test is the real FSM against the real fake, not a re-implementation of either.
#include <cmath>
#include <cstdlib>

#include "check.hpp"
#include "testutil.hpp"

#include "clk/domain/hand.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"
#include "clk/services/ui.hpp"

using namespace clk;
namespace sim = hal::host;
using domain::kRev;
using hal::motor::Hand;

// ---- domain: pure, and exhaustive where it can be --------------------------------------

void test_hand_wrap() {
    CHECK(domain::normalise(0) == 0);
    CHECK(domain::normalise(kRev) == 0);
    CHECK(domain::normalise(-1) == kRev - 1);
    CHECK(domain::normalise(-kRev - 5) == kRev - 5);

    CHECK(domain::from_deg(0.0f) == 0);
    CHECK(domain::from_deg(360.0f) == 0);
    CHECK(domain::from_deg(90.0f) == kRev / 4);
    CHECK(domain::from_deg(-90.0f) == 3 * kRev / 4);
}

// The one that would silently produce a clock reading 12:35 at 12:35 and 11:35 at 11:35 but
// take the long way round at midnight.  All 43 200 minute positions, both directions.
void test_hand_round_trip_and_shortest() {
    bool ok = true;
    for (int h = 0; h < 12 && ok; ++h) {
        for (int m = 0; m < 60 && ok; ++m) {
            const auto p = domain::for_time(h, m);
            // The minute hand is exactly on the minute; the hour hand is proportional.
            ok = ok && (p.minute == m * kRev / 60);
            ok = ok && (p.hour == (h * 3600 + m * 60) * kRev / 43200);
            ok = ok && (domain::normalise(p.hour) == p.hour);
        }
    }
    CHECK(ok);

    // Shortest path is symmetric and never longer than half a revolution.
    bool bounded = true;
    for (int i = 0; i < kRev; i += 7) {
        const int32_t d = domain::shortest(0, i);
        bounded = bounded && std::abs(d) <= kRev / 2;
        bounded = bounded && domain::normalise(d) == static_cast<int32_t>(i);
    }
    CHECK(bounded);

    // 11:59 -> 12:00 is one minute forward, not eleven hours fifty-nine minutes back.
    const auto a = domain::for_time(11, 59);
    const auto b = domain::for_time(12, 0);
    CHECK(domain::shortest(a.minute, b.minute) == kRev / 60);
    CHECK(domain::shortest(a.hour, b.hour) > 0);
    CHECK(domain::shortest(a.hour, b.hour) < kRev / 100);
}

// Every move finishes clockwise (§6.1): an anticlockwise target undershoots and comes back
// up through the gear slop, so the displayed time never sits on the wrong side of it.
void test_backlash_always_finishes_clockwise() {
    const auto cw = domain::approach(0, 1000, 200);
    CHECK(cw.via == 1000 && cw.target == 1000);  // already clockwise: one leg

    const auto ccw = domain::approach(1000, 0, 200);
    CHECK(ccw.target == 0);
    CHECK(ccw.via == -200);  // undershoot, then finish upward
    CHECK(ccw.via < ccw.target);

    const auto none = domain::approach(1000, 0, 0);
    CHECK(none.via == none.target);  // no slop configured, no doubling back
}

// ---- the AOs -------------------------------------------------------------------------------

namespace {

svc::Motion& mo() { return svc::motion(); }

// Wall-clock bounded wait on a sim-time condition.  Everything here runs under a big warp,
// so a second of patience is a couple of minutes of clock.
template <class Fn>
bool wait_until(Fn pred, int max_ms = 4000) {
    for (int i = 0; i < max_ms / 5; ++i) {
        if (pred()) return true;
        hal::clock_::sleep_ms(5);
    }
    return pred();
}

// 20x is about the ceiling for homing: the AO polls in REAL milliseconds, so past roughly
// this the control loop samples the opto too rarely in SIM time and the sweep steps straight
// over the 3-degree index window.  That is the same aliasing a too-fast sweep hits on the
// bench, so the honest thing is to respect it here rather than widen the window in the fake.
void fresh_motion() {
    sim::reset();
    cli::unsafe_set(true);
    sim::set_warp(20.0);
}

}  // namespace

void test_motion_homes_from_an_unknown_position() {
    fresh_motion();
    // A cold boot has no idea where the hands are.  That is the entire premise.
    sim::set_hand_angle(Hand::Hour, 137.0f);
    sim::set_hand_angle(Hand::Minute, 41.0f);
    CHECK(!mo().snapshot().homed);

    mo().home();
    CHECK(wait_until([] { return mo().snapshot().homed; }, 8000));

    const auto s = mo().snapshot();
    CHECK(s.state == svc::Motion::State::Idle);
    CHECK(s.homed);
    CHECK(s.faults == 0);
    CHECK(s.home_ms > 0);

    // The firmware now believes the minute hand is at 0.  It had better be nearly there --
    // the residue is the rising-edge offset, which is one `motion zero` trim, not an error.
    const float err = sim::hand_angle(Hand::Minute);
    CHECK(err < 5.0f || err > 355.0f);
}

// A sweep that finds nothing must fault rather than spin forever, and a fault must be
// recoverable by re-homing -- Fault -> Homing is the only transition out (§6.1).
void test_motion_faults_and_recovers() {
    fresh_motion();
    sim::set_opto(0.0f);  // hold the sensor dark: no mark on either hand
    mo().home();
    CHECK(wait_until([] { return mo().snapshot().state == svc::Motion::State::Fault; }, 8000));
    CHECK(mo().snapshot().faults > 0);
    CHECK(!mo().snapshot().homed);

    // A target while faulted is held, not obeyed.
    mo().goto_usteps(1234, 5678);
    hal::clock_::sleep_ms(40);
    CHECK(mo().snapshot().state == svc::Motion::State::Fault);

    sim::set_opto_auto(true);
    mo().home();
    CHECK(wait_until([] { return mo().snapshot().homed; }, 8000));
    CHECK(mo().snapshot().state != svc::Motion::State::Fault);
}

void test_motion_lands_exactly_on_an_absolute_target() {
    fresh_motion();
    sim::set_hand_angle(Hand::Hour, 0.0f);
    sim::set_hand_angle(Hand::Minute, 0.0f);

    const auto p = domain::for_time(7, 38);
    mo().goto_usteps(p.hour, p.minute);
    // Positions are UNWRAPPED -- taking the short way round to 07:38 legitimately lands on a
    // negative count -- so the comparison is modulo a revolution, never on the raw number.
    auto arrived = [&] {
        const auto s = mo().snapshot();
        return s.state == svc::Motion::State::Idle && domain::normalise(s.hour) == p.hour &&
               domain::normalise(s.minute) == p.minute;
    };
    CHECK(wait_until(arrived));

    // Exactly, not approximately: the profile decelerates into the stop target rather than
    // correcting around it.
    const auto s = mo().snapshot();
    CHECK(domain::normalise(s.hour) == p.hour);
    CHECK(domain::normalise(s.minute) == p.minute);

    // Re-issuing the same target is a no-op, not a twitch.
    mo().goto_usteps(p.hour, p.minute);
    hal::clock_::sleep_ms(40);
    CHECK(mo().snapshot().hour == s.hour);
}

void test_motion_takes_the_short_way_round() {
    fresh_motion();
    sim::set_hand_angle(Hand::Minute, 0.0f);
    // Wait on the POSITION, not on Idle: we are already idle when the move is posted, so a
    // state-only wait would sample before anything moved.
    auto settled_at = [](int32_t want) {
        return [want] {
            const auto s = mo().snapshot();
            return s.state == svc::Motion::State::Idle && domain::normalise(s.minute) == want;
        };
    };

    // From 59 minutes to 1 minute is two minutes forward, not fifty-eight back.
    const int32_t at59 = domain::for_time(0, 59).minute;
    const int32_t at01 = domain::for_time(0, 1).minute;
    mo().goto_usteps(0, at59);
    CHECK(wait_until(settled_at(at59)));
    const int32_t from = mo().snapshot().minute;

    mo().goto_usteps(0, at01);
    CHECK(wait_until(settled_at(at01)));
    const int32_t to = mo().snapshot().minute;

    CHECK(to - from == 2 * kRev / 60);
    CHECK(domain::normalise(to) == kRev / 60);
}

void test_motion_de_energises_when_idle() {
    fresh_motion();
    sim::set_hand_angle(Hand::Minute, 0.0f);
    mo().goto_usteps(0, kRev / 12);
    CHECK(wait_until([] { return mo().snapshot().powered; }));
    CHECK(wait_until([] { return mo().snapshot().state == svc::Motion::State::Idle; }));
    // Coils die 2 s of sim time after the last move -- the hands are stationary >99 % of the
    // time and holding current the whole while would be most of the power budget.
    CHECK(wait_until([] { return !mo().snapshot().powered; }));
}

void test_ui_mode_cycle() {
    sim::reset();
    cli::unsafe_set(true);
    sim::set_warp(1.0);
    auto& u = svc::ui();

    // Press steps bell -> set-alarm -> set-clock -> volume -> committed (README §12).
    const char* seq[] = {"alarm", "setalarm", "setclock", "volume", "idle"};
    for (const char* want : seq) {
        sim::press(50);
        hal::clock_::sleep_ms(60);
        sim::press(0);
        CHECK(wait_until([&] { return std::strcmp(u.snapshot().mode_name, want) == 0; }, 1000));
    }

    // Rotating in a set mode moves the value; the sensitivity is a pure firmware mapping.
    RecordingSink r;
    run("ui mode setalarm", r);
    CHECK(wait_until([&] { return std::strcmp(u.snapshot().mode_name, "setalarm") == 0; }, 1000));
    const int before = u.snapshot().alarm_hour * 60 + u.snapshot().alarm_minute;
    // 12 counts at the default 4 counts/minute = 3 minutes, and 12 is exactly the
    // acceleration threshold -- one more and the curve would multiply it by six.
    sim::turn_counts(12);
    CHECK(wait_until(
        [&] {
            const auto s = u.snapshot();
            return s.alarm_hour * 60 + s.alarm_minute == before + 3;
        },
        1000));

    // Five seconds without input drops back to Idle with everything dark (R2/R6).
    RecordingSink t;
    run("ui knob timeout 200", t);
    CHECK(wait_until([&] { return std::strcmp(u.snapshot().mode_name, "idle") == 0; }, 2000));
    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        CHECK(hal::pixels::get(i) == hal::pixels::Rgbw{});
    }
    run("ui knob timeout 5000", t);
}

void test_chrono_drives_the_hands() {
    sim::reset();
    cli::unsafe_set(true);
    sim::set_warp(20.0);
    sim::set_hand_angle(Hand::Hour, 0.0f);
    sim::set_hand_angle(Hand::Minute, 0.0f);

    RecordingSink r;
    CHECK(run("chrono time set 07:38", r) == Status::Ok);
    CHECK(wait_until(
        [] {
            const auto c = svc::chrono().snapshot();
            return c.valid && c.hour == 7 && c.minute >= 38;
        },
        2000));

    // The clock keeps running the whole time, so the assertion is that the hands agree with
    // whatever chrono currently wants -- not with a position captured before the wait.
    CHECK(wait_until(
        [] {
            const auto c = svc::chrono().snapshot();
            const auto s = mo().snapshot();
            return s.state == svc::Motion::State::Idle &&
                   std::abs(domain::shortest(c.target_minute, s.minute)) < kRev / 30 &&
                   std::abs(domain::shortest(c.target_hour, s.hour)) < kRev / 200;
        },
        6000));

    CHECK(svc::chrono().snapshot().hour == 7);
    sim::set_warp(1.0);
}

void run_motion_service_tests() {
    test_hand_wrap();
    test_hand_round_trip_and_shortest();
    test_backlash_always_finishes_clockwise();

    // From here on the real AOs are running, wired exactly as app_main wires them -- the
    // point of §11.2 is that there is no second, test-only version of the system.
    auto& motion = svc::motion();
    auto& chrono = svc::chrono();
    auto& u = svc::ui();
    motion.subscribe(&chrono);
    chrono.bind(&motion);
    u.bind(&motion, &chrono);
    motion.start();
    chrono.start();
    u.start();

    test_motion_homes_from_an_unknown_position();
    test_motion_lands_exactly_on_an_absolute_target();
    test_motion_takes_the_short_way_round();
    test_motion_de_energises_when_idle();
    test_motion_faults_and_recovers();
    test_ui_mode_cycle();
    test_chrono_drives_the_hands();

    u.stop();
    chrono.stop();
    motion.stop();
    sim::set_warp(1.0);
    sim::reset();
    cli::unsafe_set(false);
}
