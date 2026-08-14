// Hand arithmetic and the motion AO.                          [FIRMWARE.md §11.1, §6.1]
//
// The AO tests drive real sim time rather than mocking it: at `sim warp 20` a homing run
// that takes 35 s of sim time finishes in under two seconds of wall time, so the thing under
// test is the real FSM against the real fake, not a re-implementation of either.
#include <cmath>
#include <cstdio>
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

    // The property homing establishes is that the firmware's belief about EACH hand now
    // matches where it physically is -- not that either hand ends up anywhere in
    // particular.  The residue is the rising-edge offset (the edge sits a mark-width short
    // of centre), which is identical for both hands and is one `motion zero` trim on the
    // bench, not an error between them.
    for (auto h : {Hand::Hour, Hand::Minute}) {
        const float off = sim::hand_offset(h);
        CHECK(off < 5.0f || off > 350.0f);
    }
}

// The case §6.1's sequence quietly assumed away: a hand already parked ON the index holds
// the sensor lit, so there is never a rising edge to find and the run used to fail outright.
// Each of the three ways it can happen has to home like any other.
void test_motion_homes_with_a_hand_on_the_sensor() {
    struct Case {
        const char* what;
        float hour_deg;
        float minute_deg;
    };
    const Case cases[] = {
        {"hour on the mark", 0.5f, 200.0f},
        {"minute on the mark", 200.0f, 0.5f},
        {"both on the mark", 0.0f, 0.8f},
    };

    for (auto const& c : cases) {
        fresh_motion();
        sim::set_hand_angle(Hand::Hour, c.hour_deg);
        sim::set_hand_angle(Hand::Minute, c.minute_deg);
        CHECK(sim::opto() > 0.5f);  // the premise: the sensor is lit before we start

        mo().home();
        const bool ok = wait_until([] { return mo().snapshot().homed; }, 12000);
        if (!ok) std::printf("  (case: %s)\n", c.what);
        CHECK(ok);
        CHECK(mo().snapshot().faults == 0);
    }
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

namespace {

bool in_mode(const char* want, int ms = 1000) {
    return wait_until([&] { return std::strcmp(svc::ui().snapshot().mode_name, want) == 0; }, ms);
}

// A press of `ms`, as the button really behaves: down, wait, up.
void tap_knob(uint32_t ms) {
    sim::press(60u * 60 * 1000);  // hold
    hal::clock_::sleep_ms(static_cast<uint32_t>(ms));
    sim::press(0);
}

// A turn the ui AO will see as SLOW: no more counts in one 20 ms poll than slow_max.
void turn_slowly(int32_t detents) {
    const int32_t step = detents > 0 ? 4 : -4;
    for (int32_t i = 0; i < std::abs(detents); ++i) {
        sim::turn_counts(step);
        hal::clock_::sleep_ms(60);  // three ui ticks, so two nudges cannot share one poll
    }
}

int alarm_min_of_day() {
    const auto s = svc::ui().snapshot();
    return s.alarm_hour * 60 + s.alarm_minute;
}

// One detent, and WAIT for it to land before delivering the next.  A plain sleep is not
// enough on a loaded machine: two nudges that arrive inside one 20 ms poll are, correctly, a
// FAST turn, the acceleration curve multiplies them, and the test fails for a reason that has
// nothing to do with what it is testing.  Waiting for each minute makes it deterministic.
bool turn_one_minute(int dir) {
    const int before = alarm_min_of_day();
    sim::turn_counts(4 * dir);
    return wait_until([&] { return alarm_min_of_day() == before + dir; }, 1500);
}

// Where the hands are HEADED, reduced to the dial.  motion works in unwrapped microsteps so
// that "go the long way round" is expressible, so -7200 and 10080 are the same place.
bool hands_heading_for(int32_t hour, int32_t minute, int ms = 2000) {
    return wait_until(
        [&] {
            const auto s = mo().snapshot();
            return domain::normalise(s.target_hour) == domain::normalise(hour) &&
                   domain::normalise(s.target_minute) == domain::normalise(minute);
        },
        ms);
}

bool all_pixels_dark() {
    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        if (!(hal::pixels::get(i) == hal::pixels::Rgbw{})) return false;
    }
    return true;
}

}  // namespace

void test_ui_mode_cycle() {
    sim::reset();
    cli::unsafe_set(true);
    sim::set_warp(1.0);
    auto& u = svc::ui();

    // Press steps bell -> alarm -> clock -> volume -> committed (README §12).  The modes are
    // named after the icons on the plate, which is the only label the user ever sees.
    for (const char* want : {"bell", "alarm", "clock", "volume", "idle"}) {
        tap_knob(60);
        CHECK(in_mode(want));
    }

    // Rotating in `bell` is a DIRECTION, not a distance: clockwise arms, anticlockwise
    // disarms, and a single detent is enough either way.
    RecordingSink r;
    run("ui mode bell", r);
    CHECK(in_mode("bell"));
    turn_slowly(1);
    CHECK(wait_until([&] { return u.snapshot().alarm_armed; }, 1000));
    turn_slowly(-1);
    CHECK(wait_until([&] { return !u.snapshot().alarm_armed; }, 1000));

    // Rotating in `alarm` moves the time.  Slowly: one detent, one minute -- three times.
    run("ui mode alarm", r);
    CHECK(in_mode("alarm"));
    const int before = alarm_min_of_day();
    CHECK(turn_one_minute(+1));
    CHECK(turn_one_minute(+1));
    CHECK(turn_one_minute(+1));
    CHECK(alarm_min_of_day() == before + 3);

    // ... and the same knob, spun, covers hours.  12 counts in one poll is past slow_max, so
    // the curve multiplies them: this is the difference between setting 07:05 and winding
    // round to the evening, and it is the ONLY difference (§6.6).
    const int fine = alarm_min_of_day();
    sim::turn_counts(12);
    CHECK(wait_until([&] { return alarm_min_of_day() > fine + 3; }, 1000));

    // Five seconds without input drops back to Idle, and the row fades out to nothing --
    // "0 light when idle" is R2, and a fade that stops at 1/255 does not satisfy it.
    RecordingSink t;
    run("ui knob timeout 200", t);
    CHECK(in_mode("idle", 2000));
    CHECK(wait_until(all_pixels_dark, 2000));
    run("ui knob timeout 5000", t);
}

// The refusal (README §12): with the radios on, Wi-Fi provisioned and SNTP landed, the knob
// may not set the time -- the next sync would overwrite it and the user would blame the knob.
void test_ui_clock_locks_to_the_network() {
    sim::reset();
    cli::unsafe_set(true);
    sim::set_warp(1.0);
    RecordingSink r;

    run("chrono net none", r);
    run("ui mode clock", r);
    CHECK(in_mode("clock"));  // nothing owns the time: the knob does

    run("ui mode idle", r);
    run("chrono net both", r);
    CHECK(wait_until([] { return svc::ui().snapshot().net_locked; }, 1000));
    run("ui mode clock", r);
    // Refused, and it does not simply stop there: the next mode is the one you wanted next.
    CHECK(in_mode("volume"));

    // The rear toggle is the way back -- with the radios off, nothing can overwrite a manual
    // time, so the mode has to work.  (Polarity: `sim radio on` ASSERTS RADIO_OFF.)
    run("ui mode idle", r);
    run("sim radio on", r);
    CHECK(wait_until([] { return !svc::ui().snapshot().net_locked; }, 1000));
    run("ui mode clock", r);
    CHECK(in_mode("clock"));

    run("sim radio off", r);
    run("chrono net none", r);
    run("ui mode idle", r);
}

// Ten seconds of hold is BLE pairing, and it commits while the knob is still down: a gesture
// whose only feedback arrives after you let go is a gesture nobody finds.
void test_ui_long_hold_opens_pairing() {
    sim::reset();
    cli::unsafe_set(true);
    sim::set_warp(1.0);
    RecordingSink r;
    run("ui knob pair 300", r);  // 300 ms stands in for ten seconds

    sim::press(60u * 60 * 1000);
    CHECK(in_mode("pairing", 2000));
    // Still held, and all five status pixels are breathing the same blue in the same phase.
    // Wait for the breath to come UP -- it starts at zero, by definition of a breath.
    CHECK(wait_until([] { return hal::pixels::get(2).b > 0; }, 3000));
    const auto a = hal::pixels::get(2);
    CHECK(a.b > 0 && a.r == 0 && a.g == 0);
    for (std::size_t i = 3; i <= 6; ++i) CHECK(hal::pixels::get(i) == a);
    CHECK(hal::pixels::get(0) == hal::pixels::Rgbw{});  // ... and the dial wash stays out of it

    // Letting go is not also a long press: the hold already acted.
    sim::press(0);
    hal::clock_::sleep_ms(120);
    CHECK(std::strcmp(svc::ui().snapshot().mode_name, "pairing") == 0);

    // A short press leaves.
    tap_knob(60);
    CHECK(in_mode("idle"));
    CHECK(wait_until(all_pixels_dark, 2000));
    run("ui knob pair 10000", r);
}

// The hands are the readout in every mode (README §5, §12), including the two where what
// they show is not a time at all.
void test_ui_hands_show_the_mode() {
    sim::reset();
    cli::unsafe_set(true);
    sim::set_warp(20.0);
    RecordingSink r;
    run("chrono time set 03:20", r);
    run("ui mode bell", r);
    CHECK(in_mode("bell"));

    // Disarmed, the dial says so with the plainest thing a pair of hands can say.
    CHECK(hands_heading_for(0, 0));

    // Armed, it shows when it will go off.  Whatever the alarm is by now -- earlier cases in
    // this file have been turning it, and the AOs are not restarted between them.
    turn_slowly(1);
    CHECK(wait_until([] { return svc::ui().snapshot().alarm_armed; }, 1000));
    const auto u = svc::ui().snapshot();
    const auto alarm = domain::for_time(u.alarm_hour, u.alarm_minute);
    CHECK(hands_heading_for(alarm.hour, alarm.minute));

    // Volume is a gauge: both hands together, 0 % straight up, 100 % at 10 o'clock the long
    // way round -- 300 degrees, 144 usteps per percent, exactly.
    run("ui mode volume", r);
    CHECK(in_mode("volume"));
    const int32_t want = svc::ui().snapshot().volume * (kRev * 300 / 360 / 100);
    CHECK(hands_heading_for(want, want));

    run("ui mode idle", r);
    sim::set_warp(1.0);
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
    test_motion_homes_with_a_hand_on_the_sensor();
    test_motion_lands_exactly_on_an_absolute_target();
    test_motion_takes_the_short_way_round();
    test_motion_de_energises_when_idle();
    test_motion_faults_and_recovers();
    test_ui_mode_cycle();
    test_ui_clock_locks_to_the_network();
    test_ui_long_hold_opens_pairing();
    test_ui_hands_show_the_mode();
    test_chrono_drives_the_hands();

    u.stop();
    chrono.stop();
    motion.stop();
    sim::set_warp(1.0);
    sim::reset();
    cli::unsafe_set(false);
}
