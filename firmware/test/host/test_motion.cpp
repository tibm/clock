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

// Shortest is right for a clock and wrong for a knob.  This is the arithmetic under §16b.15's
// minute-hand reversal: from 12:00, thirty-one minutes FORWARD is more than half a turn, so
// the shortest way there is twenty-nine minutes BACK -- and the hand ran backwards under a
// steady clockwise turn while the hour hand, twelve times slower, looked perfect throughout.
void test_hand_directed_and_chase() {
    CHECK(domain::directed(0, kRev / 4, +1) == kRev / 4);
    CHECK(domain::directed(0, kRev / 4, -1) == kRev / 4 - kRev);
    CHECK(domain::directed(0, kRev / 4, 0) == domain::shortest(0, kRev / 4));
    // A hand already on its target does not set off round the dial to arrive back where it is.
    CHECK(domain::directed(0, 0, +1) == 0);
    CHECK(domain::directed(0, 0, -1) == 0);
    CHECK(domain::directed(kRev, 0, +1) == 0);
    // ... and the wrap is a step over 12:00, not a lap of the dial.
    CHECK(domain::directed(kRev - 10, 10, +1) == 20);
    CHECK(domain::directed(10, kRev - 10, -1) == -20);

    // From everywhere, to everywhere: under one revolution, correctly signed, and it lands.
    bool ok = true;
    for (int32_t from = 0; from < kRev && ok; from += 97) {
        for (int32_t to = 0; to < kRev && ok; to += 89) {
            const int32_t cw = domain::directed(from, to, +1);
            const int32_t ccw = domain::directed(from, to, -1);
            ok = ok && cw >= 0 && cw < kRev && ccw <= 0 && ccw > -kRev;
            ok = ok && domain::normalise(from + cw) == to;
            ok = ok && domain::normalise(from + ccw) == to;
        }
    }
    CHECK(ok);

    const int32_t at31 = domain::for_time(0, 31).minute;
    CHECK(domain::shortest(0, at31) < 0);          // the bug, stated
    CHECK(domain::directed(0, at31, +1) == at31);  // ... and the fix
    CHECK(domain::chase(0, at31) == at31);

    // chase() follows an UNWRAPPED setpoint: the way it lies, whole revolutions dropped.  The
    // knob can wind a value three turns past a hand that moves at 6000 usteps/s, and those
    // three turns are invisible -- the last twenty minutes are not.
    const int32_t twenty = kRev / 3;
    CHECK(domain::chase(0, 3 * kRev + twenty) == twenty);
    CHECK(domain::chase(0, -3 * kRev - twenty) == -twenty);
    CHECK(domain::chase(500, 500) == 0);
    CHECK(domain::chase(0, kRev) == 0);  // exactly a whole turn ahead is where we already are

    // The backlash policy applies to whatever the move turned out to be, not to a fresh guess
    // at it -- re-deriving the direction from the endpoints here would throw the choice away.
    const auto back = domain::approach_by(1000, -8000, 200);
    CHECK(back.target == -7000 && back.via == -7200);
    const auto fwd = domain::approach_by(1000, 8000, 200);
    CHECK(fwd.via == fwd.target && fwd.target == 9000);
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

// A knob step must land in the hand's OWN turn of the dial.  Callers speak in dial positions,
// which are ambiguous by a whole revolution; the hands are counted unwrapped and can be
// several turns from zero.  So "go to 0, clockwise" given to a hand sitting at 17280 means the
// zero it is standing on, not the one the number was written as -- get that wrong and the
// first knob step after any mode entry that crossed the 12 throws the hand a whole turn back.
//
// (Found by the Playwright suite, which homes first and enters `alarm` from `bell`, so its
// hands were a turn up.  The host cases had theirs near zero and passed regardless -- §11.3.)
void test_motion_a_step_lands_in_the_hands_own_turn() {
    fresh_motion();
    auto settled_at = [](int32_t want) {
        return [want] {
            const auto s = mo().snapshot();
            return s.state == svc::Motion::State::Idle && s.minute == want;
        };
    };
    // `sim::reset()` moves the hands under `motion`, which publishes on its own tick -- a
    // start position read out of the snapshot before then is a measurement from somewhere the
    // hand is not.
    CHECK(wait_until(settled_at(0), 2000));
    const int32_t zero = mo().snapshot().minute;

    // Out to the 6 and back to the 12 the shortest way, which from the 6 is CLOCKWISE: the
    // hand is now a whole revolution past where it started, and reads 12 o'clock either way.
    mo().goto_usteps(0, domain::normalise(zero + kRev / 2));
    CHECK(wait_until(settled_at(zero + kRev / 2)));
    mo().goto_usteps(0, domain::normalise(zero));
    CHECK(wait_until(settled_at(zero + kRev)));

    // One minute clockwise from there is one minute, not a turn less one minute.
    const int32_t at12 = mo().snapshot().minute;
    mo().goto_usteps(0, domain::normalise(at12 + kRev / 60), true, +1);
    CHECK(wait_until(settled_at(at12 + kRev / 60)));

    // ... and the same going the other way.
    mo().goto_usteps(0, domain::normalise(at12), true, -1);
    CHECK(wait_until(settled_at(at12)));
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

// A fresh fake, and a beat for `ui` to swallow what it did to the knob.  `sim::reset()` puts
// the PCNT count back to zero, which the next poll reads as one enormous anticlockwise turn:
// harmless in Idle, where a turn does nothing, and a scrambled setting anywhere else.
void fresh_ui(double warp = 1.0) {
    RecordingSink r;
    run("ui mode idle", r);
    sim::reset();
    cli::unsafe_set(true);
    sim::set_warp(warp);
    hal::clock_::sleep_ms(60);
}

// One count, one unit, at any speed: `sim knob 40` is exactly forty minutes on any machine,
// however loaded.  The acceleration curve has its own case (§6.6d) and is noise in these.
void knob_one_to_one(uint32_t timeout_ms) {
    RecordingSink r;
    run("ui knob counts 1", r);
    run("ui knob slow 1000000", r);  // never past slow_max, so the gain is always 1
    char line[40];
    std::snprintf(line, sizeof line, "ui knob timeout %u", timeout_ms);
    run(line, r);
}

void knob_defaults() {
    RecordingSink r;
    run("ui knob counts 4", r);
    run("ui knob slow 4", r);
    run("ui knob timeout 5000", r);
}

void motion_speed(int32_t v_max, int32_t accel) {
    RecordingSink r;
    char line[48];
    std::snprintf(line, sizeof line, "motion tune v_max %d", v_max);
    run(line, r);
    std::snprintf(line, sizeof line, "motion tune accel %d", accel);
    run(line, r);
}

// What a pixel DID over a window, which is the only way to tell a breath from a blink: a
// breath is a curve through many levels, a blink is one level with square edges.  Same
// distinction the Playwright suite makes on the swatch (§11.3), same window.
struct Watched {
    int levels = 0;  // distinct non-zero brightnesses seen
    bool ever_dark = false;
    bool ever_lit = false;
    hal::pixels::Rgbw peak{};
};

Watched watch_pixel(std::size_t i, int ms) {
    auto sum = [](hal::pixels::Rgbw c) { return static_cast<int>(c.r) + c.g + c.b + c.w; };
    int seen[64];
    int n = 0;
    Watched w;
    for (int t = 0; t < ms; t += 4) {
        const auto c = hal::pixels::get(i);
        const int s = sum(c);
        if (s == 0) {
            w.ever_dark = true;
        } else {
            w.ever_lit = true;
            if (s > sum(w.peak)) w.peak = c;
            bool known = false;
            for (int k = 0; k < n; ++k) known = known || seen[k] == s;
            if (!known && n < 64) seen[n++] = s;
        }
        hal::clock_::sleep_ms(4);
    }
    w.levels = n;
    return w;
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

    // ... and a SPIN is worth every minute you spun it, delivered at the speed the hands can
    // draw (§6.6d, changed 2026-08-16).  40 counts is ten minutes at 4 counts a minute: they
    // are banked, not multiplied, and they arrive one at a time rather than in a jump the
    // dial cannot render.
    const int fine = alarm_min_of_day();
    sim::turn_counts(40);
    CHECK(alarm_min_of_day() < fine + 10);  // not all at once ...
    CHECK(wait_until([&] { return alarm_min_of_day() == fine + 10; }, 3000));  // ... but all

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

    // And pairing obeys the ONE timeout, like every other mode: it used to have its own two
    // minutes, which is a second rule to learn about a control with no labels (§6.6c, changed
    // 2026-08-15).  A finger on the knob still counts as input, so the hold itself is safe.
    run("ui knob timeout 400", r);
    sim::press(60u * 60 * 1000);
    CHECK(in_mode("pairing", 2000));
    hal::clock_::sleep_ms(900);
    CHECK(std::strcmp(svc::ui().snapshot().mode_name, "pairing") == 0);  // held: not idle yet
    sim::press(0);
    CHECK(in_mode("idle", 2000));

    run("ui knob timeout 5000", r);
    run("ui knob pair 10000", r);
}

// The hands are the readout in every mode (README §5, §12), including the two where what
// they show is not a time at all.
void test_ui_hands_show_the_mode() {
    fresh_ui(4.0);
    RecordingSink r;
    run("ui knob timeout 60000", r);  // this case reads the dial between gestures
    run("chrono time set 03:20", r);
    run("ui mode bell", r);
    CHECK(in_mode("bell"));
    turn_slowly(-1);  // whatever the last case left armed
    CHECK(wait_until([] { return !svc::ui().snapshot().alarm_armed; }, 4000));

    // Disarmed, both hands go to the 6 -- stacked, which is a reading no working clock can
    // produce (at 6:30 the hour hand is halfway to the 7).  The dial is saying something
    // rather than showing a plausible wrong time (§6.6b, changed 2026-08-15).
    CHECK(hands_heading_for(kRev / 2, kRev / 2, 8000));

    // Armed, it shows when it will go off.  Whatever the alarm is by now -- earlier cases in
    // this file have been turning it, and the AOs are not restarted between them.
    turn_slowly(1);
    CHECK(wait_until([] { return svc::ui().snapshot().alarm_armed; }, 4000));
    const auto u = svc::ui().snapshot();
    const auto alarm = domain::for_time(u.alarm_hour, u.alarm_minute);
    CHECK(hands_heading_for(alarm.hour, alarm.minute, 8000));

    // Volume is a gauge: both hands together, 0 % straight up, 100 % at 10 o'clock the long
    // way round -- 300 degrees, 144 usteps per percent, exactly.
    run("ui mode volume", r);
    CHECK(in_mode("volume"));
    const int32_t want = svc::ui().snapshot().volume * (kRev * 300 / 360 / 100);
    CHECK(hands_heading_for(want, want, 8000));

    run("ui mode idle", r);
    knob_defaults();
    sim::set_warp(1.0);
}

// Modes 1 and 2 answer the same question, so they say it the same way: both states BREATHE
// and the answer is the colour.  A fast blink reads as an alarm going OFF rather than an
// alarm that is set, and this is a bedside light (§6.6b, changed 2026-08-15).
void test_ui_alarm_cue_breathes_either_way() {
    fresh_ui(1.0);
    RecordingSink r;
    run("ui knob timeout 60000", r);
    run("ui anim breathe 400", r);  // a whole breath inside one sampling window
    constexpr std::size_t kBell = 2, kAlarmPx = 3;

    for (const char* mode : {"bell", "alarm"}) {
        const std::size_t px = std::strcmp(mode, "bell") == 0 ? kBell : kAlarmPx;
        char line[24];
        std::snprintf(line, sizeof line, "ui mode %s", mode);

        run("ui mode bell", r);
        CHECK(in_mode("bell"));
        turn_slowly(-1);  // disarm
        CHECK(wait_until([] { return !svc::ui().snapshot().alarm_armed; }, 1000));
        run(line, r);
        CHECK(in_mode(mode));
        const auto off = watch_pixel(px, 500);
        CHECK(off.ever_lit);
        CHECK(off.ever_dark);            // a breath reaches zero
        CHECK(off.levels > 4);           // ... through many levels, which a blink does not
        CHECK(off.peak.w > off.peak.r);  // white: the W die

        run("ui mode bell", r);
        CHECK(in_mode("bell"));
        turn_slowly(1);  // arm
        CHECK(wait_until([] { return svc::ui().snapshot().alarm_armed; }, 1000));
        run(line, r);
        CHECK(in_mode(mode));
        const auto on = watch_pixel(px, 500);
        CHECK(on.ever_lit);
        CHECK(on.ever_dark);
        CHECK(on.levels > 4);  // armed BREATHES now; it used to be a hard-edged blink
        CHECK(on.peak.r > 0 && on.peak.g == 0 && on.peak.b == 0 && on.peak.w == 0);
    }

    run("ui anim breathe 3200", r);
    run("ui mode idle", r);
    knob_defaults();
}

// `clock` opens on the time the clock is keeping -- and on 12:00 when nothing has ever told
// it one.  chrono's hour and minute are an offset from an epoch it never had, so an unset
// clock reads as minutes-since-boot: 00:04 is not a time, it is an uptime, and the mode used
// to open with the hands pointing at it.
//
// Runs FIRST, before any other ui case: there is no way back to an unset clock, and merely
// visiting `clock` sets one -- leaving the mode commits whatever the hands were showing.
void test_ui_clock_starts_from_the_clock() {
    fresh_ui(4.0);
    RecordingSink r;
    run("ui knob timeout 60000", r);
    CHECK(!svc::chrono().snapshot().valid);

    run("ui mode clock", r);
    CHECK(in_mode("clock"));
    CHECK(hands_heading_for(0, 0, 8000));  // 12:00, both hands

    // Wait for the mode to actually BE idle before setting a time.  Leaving `clock` commits
    // what the hands were showing (§6.6c), on the ui thread, whenever it gets there -- and a
    // `chrono time set` racing that commit is a test that sometimes reads 12:00 back.
    run("ui mode idle", r);
    CHECK(in_mode("idle"));

    run("chrono time set 04:25", r);
    // chrono publishes on its own 250 ms tick, and `enter()` reads that snapshot -- so wait
    // for the time to actually BE 04:25 rather than for the set to have been accepted.
    // `wait_until` sleeps in SIM time and chrono publishes on a REAL 250 ms tick, so at warp
    // 4 a budget of 2000 is half a second of patience -- two ticks, on a good day.
    CHECK(wait_until(
        [] {
            const auto c = svc::chrono().snapshot();
            return c.valid && c.hour == 4 && c.minute == 25;
        },
        12000));
    run("ui mode clock", r);
    CHECK(in_mode("clock"));
    const auto at = domain::for_time(4, 25);
    CHECK(hands_heading_for(at.hour, at.minute, 8000));

    run("ui mode idle", r);
    knob_defaults();
    sim::set_warp(1.0);
}

namespace {

// Two full turns of the HOUR hand in forty-minute steps, watching the minute hand every
// sample of the way.  It may never move backwards, and each step must move it the whole 240
// degrees -- landing on the right minute by going round the other way is exactly the bug.
bool wind_a_day(int dir) {
    RecordingSink r;
    run("ui mode alarm", r);
    if (!in_mode("alarm")) return false;
    if (!wait_until([] { return mo().snapshot().state == svc::Motion::State::Idle; }, 6000)) {
        std::printf("  (wind: the hands never settled before the first step)\n");
        return false;
    }
    const auto start = mo().snapshot();
    int32_t last_h = start.hour, last_m = start.minute;

    constexpr int32_t kStepMin = 40;  // not a factor of 60: the minute hand has to move
    constexpr int32_t kPerMinute = kStepMin * kRev / 60;
    constexpr int32_t kPerHour = kStepMin * kRev / 720;
    static_assert(36 * kStepMin == 24 * 60, "thirty-six steps is a whole day");

    for (int step = 1; step <= 36; ++step) {
        sim::turn_counts(kStepMin * dir);
        const int32_t want_m = start.minute + dir * step * kPerMinute;
        const int32_t want_h = start.hour + dir * step * kPerHour;
        bool backwards = false;
        const bool arrived = wait_until(
            [&] {
                const auto s = mo().snapshot();
                backwards = backwards || (dir > 0 ? (s.minute < last_m || s.hour < last_h)
                                                  : (s.minute > last_m || s.hour > last_h));
                last_m = s.minute;
                last_h = s.hour;
                return s.minute == want_m && s.hour == want_h;
            },
            6000);
        if (backwards) {
            std::printf("  (wind %s: a hand went backwards during step %d)\n",
                        dir > 0 ? "forward" : "back", step);
            return false;
        }
        if (!arrived) {
            const auto s = mo().snapshot();
            std::printf("  (wind %s: step %d wanted h=%d m=%d, got h=%d m=%d)\n",
                        dir > 0 ? "forward" : "back", step, want_h, want_m, s.hour, s.minute);
            return false;
        }
    }
    // Twenty-four hours of winding is two turns of the hour hand and twenty-four of the
    // minute hand, every one of them actually travelled.
    const auto end = mo().snapshot();
    return end.hour - start.hour == dir * 2 * kRev && end.minute - start.minute == dir * 24 * kRev;
}

}  // namespace

// §16b.15: the minute hand ran BACKWARDS under a steady clockwise turn.  Winding past the half
// hour moves it more than half a revolution, and the shortest way to somewhere more than half
// a revolution ahead is behind you -- so it reversed, while the hour hand, twelve times
// slower and never near the same limit, went on looking perfect.  That asymmetry is why it
// read as "the minute hand is flaky" rather than as one wrong line.
void test_ui_winds_a_day_without_reversing() {
    fresh_ui(20.0);
    knob_one_to_one(600000);      // ten minutes: this case winds for far longer than five seconds
    motion_speed(24000, 200000);  // 24 revolutions of dial in a test, not in a minute

    CHECK(wind_a_day(+1));
    CHECK(wind_a_day(-1));

    motion_speed(6000, 20000);
    knob_defaults();
    RecordingSink r;
    run("ui mode idle", r);
    sim::set_warp(1.0);
}

// A knob that is DRAGGED, which is what a finger does.  The cases above wait for each step to
// land, so the hands are never behind; a finger does not wait, and at any speed worth calling
// a spin the counts arrive far faster than a 6000 ustep/s movement can answer.
//
// §16c: the setting outran the hands until the minute hand's target WRAPPED -- and a hand in
// flight was then re-aimed at somewhere it had already gone past, so it stopped and backed
// up.  Anticlockwise that reads as "the hour hand goes the right way and the minute hand goes
// the other"; clockwise, at an hour a poll, it reads as a minute hand that has stopped
// following the knob and started following the hour hand.  Both are the same wrap.
void test_ui_a_dragged_knob_never_reverses() {
    fresh_ui(1.0);
    RecordingSink r;
    run("ui knob timeout 60000", r);
    run("ui mode alarm", r);
    CHECK(in_mode("alarm"));
    CHECK(wait_until([] { return mo().snapshot().state == svc::Motion::State::Idle; }, 8000));

    for (const int dir : {+1, -1}) {
        int32_t last = mo().snapshot().minute;
        const int32_t from = last;
        bool wrong = false;
        auto sample = [&](int for_ms) {
            for (int i = 0; i < for_ms / 2; ++i) {
                hal::clock_::sleep_ms(2);
                const int32_t now = mo().snapshot().minute;
                wrong = wrong || (dir > 0 ? now < last : now > last);
                last = now;
            }
        };
        // Two minutes of setting every 40 ms, against a movement that can draw twenty a
        // second: the knob is asking for six times what the hands can do.
        for (int i = 0; i < 12; ++i) {
            sim::turn_counts(8 * dir);
            sample(40);
        }
        sample(2000);  // ... and while the bank pays out what is left of it

        if (wrong) std::printf("  (drag %s: a hand went backwards)\n", dir > 0 ? "CW" : "CCW");
        CHECK(!wrong);
        // It moved, and it moved a long way -- this is not "monotone because it never budged",
        // which is exactly what the clockwise half of the bug looked like.
        CHECK(dir > 0 ? last - from > kRev / 4 : last - from < -kRev / 4);
    }

    run("ui mode idle", r);
    knob_defaults();
}

// A spin is worth every minute you spun it, and no more -- delivered at the speed the hands
// can draw rather than in a jump the dial cannot render (§6.6d, changed 2026-08-16).
void test_ui_a_spin_is_banked_not_multiplied() {
    fresh_ui(1.0);
    RecordingSink r;
    run("ui knob timeout 60000", r);
    run("ui mode alarm", r);
    CHECK(in_mode("alarm"));
    const int before = alarm_min_of_day();

    sim::turn_counts(40);  // ten minutes at four counts a minute
    hal::clock_::sleep_ms(30);
    const int right_after = alarm_min_of_day();
    CHECK(right_after > before);       // something happened straight away ...
    CHECK(right_after < before + 10);  // ... but not all of it
    CHECK(wait_until([&] { return alarm_min_of_day() == before + 10; }, 3000));
    hal::clock_::sleep_ms(300);
    CHECK(alarm_min_of_day() == before + 10);  // and it stops there rather than coasting on

    // The bank is bounded: a violent spin cannot wind for the rest of the afternoon.
    const int mid = alarm_min_of_day();
    sim::turn_counts(4000);  // a thousand minutes' worth of counts
    CHECK(wait_until([&] { return alarm_min_of_day() > mid + 20; }, 8000));
    hal::clock_::sleep_ms(1500);
    CHECK(alarm_min_of_day() < mid + 200);

    run("ui mode idle", r);
    knob_defaults();
}

// The same bug at its smallest, in the other mode that has it: from 12:00, thirty-one minutes
// forward is more than half a turn of the minute hand.
void test_ui_a_wind_past_the_half_hour_goes_forwards() {
    fresh_ui(4.0);
    knob_one_to_one(60000);
    RecordingSink r;
    run("chrono time set 12:00", r);
    run("ui mode clock", r);
    CHECK(in_mode("clock"));
    CHECK(wait_until([] { return mo().snapshot().state == svc::Motion::State::Idle; }, 6000));
    const auto before = mo().snapshot();

    sim::turn_counts(31);
    const int32_t want = before.minute + 31 * kRev / 60;
    CHECK(wait_until([&] { return mo().snapshot().minute == want; }, 6000));
    // Not just "it arrived": it arrived the way the knob turned.  The shortest way here is
    // twenty-nine minutes anticlockwise, and it lands on the same place on the dial.
    CHECK(mo().snapshot().minute - before.minute > 0);
    CHECK(domain::shortest(before.minute, want) < 0);

    // ... and back the other way, which is under half a turn and was never broken.
    const auto mid = mo().snapshot();
    sim::turn_counts(-31);
    CHECK(wait_until([&] { return mo().snapshot().minute == before.minute; }, 6000));
    CHECK(mo().snapshot().minute - mid.minute < 0);

    run("ui mode idle", r);
    knob_defaults();
    sim::set_warp(1.0);
}

// The gauge is 300 degrees and the last 60 -- between the 10 and the 12 -- are off the scale.
// The hands must SWEEP it rather than cut across the dead zone: the short way from 30 % to
// 100 % goes backwards over the 12, which is both the wrong direction for a level going up
// and a trip through the one part of the dial the gauge does not use.
void test_ui_volume_sweeps_the_gauge() {
    fresh_ui(4.0);
    knob_one_to_one(60000);
    RecordingSink r;
    constexpr int32_t kFull = kRev * 300 / 360;  // 100 % -- 10 o'clock, the long way round
    constexpr int32_t kDeadFrom = kFull, kDeadTo = kRev;

    run("ui mode volume", r);
    CHECK(in_mode("volume"));
    sim::turn_counts(-200);  // hard down to 0 %, and let it get there
    CHECK(wait_until([] { return svc::ui().snapshot().volume == 0; }, 2000));
    CHECK(wait_until(
        [] {
            const auto s = mo().snapshot();
            return s.state == svc::Motion::State::Idle && domain::normalise(s.minute) == 0;
        },
        8000));

    // 0 % -> 30 % -> 100 %.  The second leg is the one that used to reverse across the 12.
    bool in_dead_zone = false;
    auto sweep_to = [&](int pct) {
        const int32_t target = pct * kRev * 300 / 360 / 100;
        sim::turn_counts(pct - static_cast<int>(svc::ui().snapshot().volume));
        return wait_until(
            [&] {
                const auto s = mo().snapshot();
                const int32_t at = domain::normalise(s.minute);
                in_dead_zone = in_dead_zone || (at > kDeadFrom && at < kDeadTo);
                return svc::ui().snapshot().volume == pct && s.state == svc::Motion::State::Idle &&
                       at == target;
            },
            8000);
    };

    const int32_t at_zero = mo().snapshot().minute;
    CHECK(sweep_to(30));
    CHECK(sweep_to(100));
    // Up is clockwise, all the way round the scale: 300 degrees of travel, not -60.
    CHECK(mo().snapshot().minute - at_zero == kFull);

    const int32_t at_full = mo().snapshot().minute;
    CHECK(sweep_to(0));
    CHECK(mo().snapshot().minute - at_full == -kFull);  // and down is anticlockwise
    CHECK(!in_dead_zone);                               // never once between the 10 and the 12

    // Both hands, together, the whole time -- a gauge with two needles that disagree is not a
    // gauge (§6.6b).
    const auto s = mo().snapshot();
    CHECK(domain::normalise(s.hour) == domain::normalise(s.minute));

    run("ui mode idle", r);
    knob_defaults();
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
    test_hand_directed_and_chase();
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
    test_motion_a_step_lands_in_the_hands_own_turn();
    test_motion_de_energises_when_idle();
    test_motion_faults_and_recovers();
    // First: an unset clock is a starting condition there is no way back to -- and merely
    // VISITING `clock` sets one, because leaving the mode commits what the hands showed.
    test_ui_clock_starts_from_the_clock();
    test_ui_mode_cycle();
    test_ui_clock_locks_to_the_network();
    test_ui_long_hold_opens_pairing();
    test_ui_alarm_cue_breathes_either_way();
    test_ui_hands_show_the_mode();
    test_ui_a_wind_past_the_half_hour_goes_forwards();
    test_ui_a_dragged_knob_never_reverses();
    test_ui_a_spin_is_banked_not_multiplied();
    test_ui_winds_a_day_without_reversing();
    test_ui_volume_sweeps_the_gauge();
    test_chrono_drives_the_hands();

    u.stop();
    chrono.stop();
    motion.stop();
    sim::set_warp(1.0);
    sim::reset();
    cli::unsafe_set(false);
}
