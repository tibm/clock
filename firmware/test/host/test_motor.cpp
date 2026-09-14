// The fake mechanism: the axis, the hands, and the opto that watches them.
// [FIRMWARE.md §11.1, §11.2, §13.9]
//
// Everything here uses `sim jump` rather than sleeping.  The axis integrates in SIM time, so
// a test can slew a hand through a full revolution in no wall time at all -- and a test that
// slept would be a test that is flaky on a loaded machine.
#include "check.hpp"
#include "testutil.hpp"

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/hal/mcp23017.hpp"

using namespace clk;
namespace sim = hal::host;
using hal::motor::Hand;
using hal::motor::kUstepsPerRev;

namespace {

void fresh() {
    sim::reset();
    cli::unsafe_set(true);
}

// Both hands parked at 12 o'clock with the firmware's bookkeeping agreeing -- the state a
// successful homing leaves behind, and the only one in which "commanded == visible".
void fresh_homed() {
    fresh();
    hal::motor::enable(true);
    sim::set_hand_angle(Hand::Hour, 0.0f);
    sim::set_hand_angle(Hand::Minute, 0.0f);
}

bool near(float a, float b, float tol = 0.05f) {
    const float d = a - b;
    return (d < 0 ? -d : d) < tol;
}

// usteps/s that walks one revolution per second, which makes the arithmetic readable.
constexpr int32_t kRevPerSec = kUstepsPerRev;

}  // namespace

// ---- the axis ----------------------------------------------------------------------------

void test_axis_integrates_in_sim_time() {
    fresh_homed();
    CHECK(hal::motor::state(Hand::Minute).pos == 0);

    // Quarter of a revolution at one rev/s = 250 ms.
    CHECK(hal::motor::run(Hand::Minute, kRevPerSec, kUstepsPerRev / 4) == Status::Ok);
    CHECK(hal::motor::state(Hand::Minute).moving);

    sim::advance(100'000);  // 100 ms = a tenth of a revolution = 36 deg
    const auto mid = hal::motor::state(Hand::Minute);
    CHECK(mid.pos > kUstepsPerRev / 10 - 40 && mid.pos < kUstepsPerRev / 10 + 40);
    CHECK(mid.moving);
    CHECK(near(sim::hand_angle(Hand::Minute), 36.0f, 1.0f));

    sim::advance(400'000);  // well past the stop
    const auto end = hal::motor::state(Hand::Minute);
    CHECK(end.pos == kUstepsPerRev / 4);  // lands exactly, never overshoots
    CHECK(!end.moving);
    CHECK(end.vel == 0);
    CHECK(near(sim::hand_angle(Hand::Minute), 90.0f));
}

void test_axis_runs_both_ways_and_holds() {
    fresh_homed();
    hal::motor::run(Hand::Hour, -kRevPerSec, -kUstepsPerRev / 2);
    sim::advance(250'000);
    CHECK(near(sim::hand_angle(Hand::Hour), 270.0f, 1.0f));  // quarter turn anticlockwise

    hal::motor::hold(Hand::Hour);
    const int32_t held = hal::motor::state(Hand::Hour).pos;
    CHECK(!hal::motor::state(Hand::Hour).moving);
    sim::advance(2'000'000);
    CHECK(hal::motor::state(Hand::Hour).pos == held);  // a held axis does not drift

    // A velocity pointing away from the target is a caller bug, not a slow move.
    CHECK(hal::motor::run(Hand::Hour, +kRevPerSec, held - 100) == Status::BadArg);
}

// The reason `pos` is a double: re-basing on every control tick while truncating to whole
// microsteps sheds up to half a step each time, which at 100 Hz is degrees per minute of
// drift that exists nowhere but in the fake.
void test_axis_does_not_shed_fractions() {
    fresh_homed();
    // Slow sim time right down so the wall-clock cost of the loop itself contributes almost
    // nothing and what is left is the arithmetic under test.  Without this the check is a
    // check on how fast the machine is -- it passes on a quiet laptop and fails under TSan.
    sim::set_warp(0.01);
    constexpr int32_t kTicks = 500;
    constexpr int32_t kVel = 1000;      // usteps/s
    constexpr uint64_t kTickUs = 1000;  // 1 ms -> exactly 1 ustep per tick
    for (int32_t i = 0; i < kTicks; ++i) {
        hal::motor::run(Hand::Minute, kVel, kUstepsPerRev);  // re-issued, same target
        sim::advance(kTickUs);
    }
    const auto a = hal::motor::state(Hand::Minute);
    CHECK(a.pos >= kTicks - 1 && a.pos <= kTicks + 1);
    sim::set_warp(1.0);
}

void test_standby_freezes_the_hands() {
    fresh_homed();
    hal::motor::run(Hand::Minute, kRevPerSec, kUstepsPerRev);
    sim::advance(100'000);

    // Dropping STEP_STBY kills the coils, and dead coils do not turn a rotor.
    CHECK(hal::motor::enable(false) == Status::Ok);
    const int32_t frozen = hal::motor::state(Hand::Minute).pos;
    sim::advance(1'000'000);
    CHECK(hal::motor::state(Hand::Minute).pos == frozen);

    // And a service that forgets the MotorPower handshake finds out here, not on a bench.
    CHECK(hal::motor::run(Hand::Minute, kRevPerSec, kUstepsPerRev) == Status::NotReady);
    CHECK(hal::motor::enable(true) == Status::Ok);
    CHECK(hal::motor::run(Hand::Minute, kRevPerSec, kUstepsPerRev) == Status::Ok);
}

void test_adopt_renames_without_moving() {
    fresh();
    hal::motor::enable(true);
    sim::set_hand_angle(Hand::Hour, 137.0f);
    hal::motor::run(Hand::Hour, kRevPerSec, 4000);
    sim::advance(1'000'000);

    const float before = sim::hand_angle(Hand::Hour);
    CHECK(hal::motor::adopt(Hand::Hour, 0) == Status::Ok);
    CHECK(hal::motor::state(Hand::Hour).pos == 0);
    // The hand did not twitch -- only the name of where it is changed.  This is exactly what
    // the homing FSM does when it finds the index.
    CHECK(near(sim::hand_angle(Hand::Hour), before));
    CHECK(!hal::motor::state(Hand::Hour).moving);
}

void test_presence_gates_the_movement() {
    fresh_homed();
    board::set_present(board::Dev::Motor, false);
    CHECK(hal::motor::enable(true) == Status::NotPresent);
    CHECK(hal::motor::run(Hand::Hour, kRevPerSec, 100) == Status::NotPresent);
    CHECK(hal::motor::hold(Hand::Hour) == Status::NotPresent);
    board::set_present(board::Dev::Motor, true);
    CHECK(hal::motor::enable(true) == Status::Ok);
}

// ---- the opto watches the hands ------------------------------------------------------------

void test_opto_follows_the_hands() {
    fresh();
    CHECK(sim::opto_auto());

    // Power-on: neither hand is near the index, so the sensor sees the dial.
    CHECK(sim::opto() < 0.2f);

    sim::set_hand_angle(Hand::Minute, 0.0f);
    CHECK(sim::opto() > 0.9f);
    sim::set_hand_angle(Hand::Minute, 359.5f);  // still inside the window, from the other side
    CHECK(sim::opto() > 0.9f);
    sim::set_hand_angle(Hand::Minute, 10.0f);
    CHECK(sim::opto() < 0.2f);

    // Either hand lights it -- which is why homing parks one 180 deg away before sweeping
    // the other (README §5).
    sim::set_hand_angle(Hand::Hour, 0.0f);
    CHECK(sim::opto() > 0.9f);

    // There is an edge, not a step: a detector that only handles a step passes here and
    // fails on the bench.
    sim::set_hand_angle(Hand::Hour, 180.0f);
    sim::set_hand_angle(Hand::Minute, 1.8f);
    const float edge = sim::opto();
    CHECK(edge > 0.2f && edge < 0.9f);
}

void test_opto_manual_override_wins() {
    fresh();
    sim::set_hand_angle(Hand::Minute, 0.0f);
    CHECK(sim::opto() > 0.9f);

    sim::set_opto(0.25f);  // a written value HOLDS -- this is what most tests want
    CHECK(!sim::opto_auto());
    CHECK(sim::opto() > 0.24f && sim::opto() < 0.26f);
    sim::set_hand_angle(Hand::Minute, 90.0f);
    CHECK(sim::opto() > 0.24f && sim::opto() < 0.26f);  // hands no longer matter

    sim::set_opto_auto(true);
    CHECK(sim::opto() < 0.2f);
}

// A sweep fast enough to cross the 3 deg window between two ADC reads misses it entirely.
// That aliasing is real -- the phototransistor is continuous, the ADC is not -- and this
// test exists so that nobody "fixes" the fake by making the window impossible to miss.
void test_a_fast_sweep_can_alias_past_the_index() {
    fresh();
    hal::motor::enable(true);
    sim::set_hand_angle(Hand::Hour, 180.0f);
    sim::set_hand_angle(Hand::Minute, 350.0f);

    // 1 rev/s, sampled every 50 ms = 18 deg between reads, against a 3 deg window.
    hal::motor::run(Hand::Minute, kRevPerSec, kUstepsPerRev);
    bool saw = false;
    for (int i = 0; i < 20; ++i) {
        sim::advance(50'000);
        if (sim::opto() > 0.5f) saw = true;
    }
    CHECK(!saw);

    // Slow enough and the same sweep finds it: 1/30 rev/s over the same window.
    fresh();
    hal::motor::enable(true);
    sim::set_hand_angle(Hand::Hour, 180.0f);
    sim::set_hand_angle(Hand::Minute, 350.0f);
    hal::motor::run(Hand::Minute, kRevPerSec / 30, kUstepsPerRev);
    saw = false;
    for (int i = 0; i < 40; ++i) {
        sim::advance(50'000);
        if (sim::opto() > 0.5f) saw = true;
    }
    CHECK(saw);
}

// ---- the sim commands on top -------------------------------------------------------------

void test_sim_mechanism_commands() {
    fresh();
    RecordingSink h;
    CHECK(run("sim hand m 42.5", h) == Status::Ok);
    CHECK(near(sim::hand_angle(Hand::Minute), 42.5f));
    CHECK(h.contains("42.5"));

    RecordingSink bad;
    CHECK(run("sim hand banana 1", bad) == Status::BadArg);

    RecordingSink m;
    CHECK(run("sim motor on", m) == Status::Ok);
    CHECK(hal::motor::enabled());
    CHECK(m.contains("coils live"));
    run("sim motor off", m);
    CHECK(!hal::motor::enabled());

    RecordingSink k;
    CHECK(run("sim knob -12", k) == Status::Ok);
    CHECK(hal::knob::read().v.count == -12);  // raw counts, not detents
    RecordingSink k0;
    CHECK(run("sim knob 0", k0) == Status::BadArg);

    RecordingSink p;
    run("sim press down", p);
    CHECK(hal::knob::read().v.sw);
    run("sim press up", p);
    CHECK(!hal::knob::read().v.sw);

    RecordingSink i;
    CHECK(run("sim imu 142", i) == Status::Ok);
    CHECK(near(hal::imu::read().v.yaw_deg, 142.0f));
    const uint16_t taps = hal::imu::read().v.taps;
    RecordingSink t;
    CHECK(run("sim tap", t) == Status::Ok);
    CHECK(hal::imu::read().v.taps == taps + 1);

    RecordingSink o;
    CHECK(run("sim opto auto", o) == Status::Ok);
    CHECK(sim::opto_auto());

    RecordingSink s;
    CHECK(run("sim speaker on", s) == Status::Ok);
    CHECK(hal::audio::active());
}

void test_radio_toggle_polarity() {
    fresh();
    // Closed/low = radios off, so a broken harness fails to radios-ENABLED and the clock
    // stays remotely diagnosable (README §16d).
    CHECK(hal::expander::get(hal::expander::Sig::RadioOff).v);  // idle high

    RecordingSink r;
    CHECK(run("sim radio on", r) == Status::Ok);
    CHECK(!hal::expander::get(hal::expander::Sig::RadioOff).v);
    CHECK(sim::snapshot().radio_off);
    CHECK(r.contains("disabled"));

    run("sim radio off", r);
    CHECK(hal::expander::get(hal::expander::Sig::RadioOff).v);
    CHECK(!sim::snapshot().radio_off);

    RecordingSink bad;
    CHECK(run("sim radio banana", bad) == Status::BadArg);
}

void test_expander_inputs_are_read_only() {
    fresh();
    using S = hal::expander::Sig;
    CHECK(hal::expander::set(S::SpkSd, true) == Status::Ok);
    CHECK(hal::expander::get(S::SpkSd).v);
    // Driving an input from firmware is not a thing the hardware can do.
    CHECK(hal::expander::set(S::RadioOff, false) == Status::BadArg);
    CHECK(hal::expander::set(S::PdPg, false) == Status::BadArg);

    // §6.8 interlock 1: the 12 V boost is gated on PD_PG, in the fake as in the schematic.
    CHECK(hal::expander::set(S::Boost12En, true) == Status::Ok);
    sim::set_plugged(false);
    CHECK(!hal::expander::get(S::Boost12En).v);  // unplugging drops it
    CHECK(hal::expander::set(S::Boost12En, true) == Status::Denied);
    sim::set_plugged(true);
    CHECK(hal::expander::set(S::Boost12En, true) == Status::Ok);

    // CHRG is open-drain: low WHILE charging, and it cannot disagree with power::read().
    sim::set_vbat_mv(3700);
    CHECK(hal::power::read().v.charging);
    CHECK(!hal::expander::get(S::Chrg).v);

    board::set_present(board::Dev::Expander, false);
    CHECK(hal::expander::get(S::SpkSd).st == Status::NotPresent);
}

// ---- hal::power -- what the tap can see, and what it cannot ----------------------------
//
// One implementation for both backends now (shared/power.cpp), so these are not "the host's
// power model": they are the arithmetic the board runs, against the fake ADC and the fake
// expander.  Milestone 1's last item shipped with these expectations, not after them.

// R-BOARD-3, which is the whole reason `State` carries a `src` at all.  `VBAT_SENSE` taps cell+
// against BOARD GND, so it is only looking at the cell while cell - is at PACK-: with the
// protector's charge FET open, cell - floats and the divider reports the charger's output
// instead.  Build #1 read 4.0 V for a cell at 3.4 V on 2026-09-13.  Nothing in firmware can see
// that FET, so the contract is to say which node the number came from -- and to refuse to turn
// the wrong one into a percentage.
void test_power_says_which_node_it_measured() {
    fresh();
    using P = hal::power::VbatSrc;
    sim::set_vbat_mv(3675);  // halfway between the 3.30 V and 4.05 V endpoints

    // On battery the board is running FROM the cell, so the tap is across it and an SoC means
    // something.  Half a cell, to within the endpoints' own arithmetic.
    sim::set_plugged(false);
    auto p = hal::power::read();
    CHECK(p.ok());
    CHECK(p.v.src == P::Cell);
    CHECK(p.v.vbat_mv == 3675);
    CHECK(p.v.soc_pct == 50);
    CHECK(!p.v.plugged);

    // Plugged, the same divider is on the BAT node.  The millivolts still come back -- the
    // bench wants them -- but there is no SoC to be had from the charger's output, and a number
    // there would be the faked reading D16 exists to forbid.
    sim::set_plugged(true);
    p = hal::power::read();
    CHECK(p.ok());
    CHECK(p.v.src == P::BatNode);
    CHECK(p.v.soc_pct == hal::power::kSocUnknown);
    CHECK(p.v.plugged);

    // And CHRG does NOT buy its way into `Cell`.  Current flowing proves the charge FET is
    // closed, but it also means the node sits I x R above the cell and the LT3652 holds CHRG
    // through its whole C/10 taper -- a cell voltage plus an unknown offset is not cell health.
    CHECK(p.v.charging);
    CHECK(p.v.src == P::BatNode);
}

// The three status lines are open-drain with the expander's pull-ups, so a LOW pin is an
// ASSERTED signal.  PD_PG was modelled the other way round on this side until 2026-09-13, which
// would have had clocksim and the target print opposite bits for the same board -- and §12.0.11
// step 6 expects `PD_PG 0` with the brick in.
void test_power_status_lines_are_active_low() {
    fresh();
    using S = hal::expander::Sig;
    sim::set_plugged(true);
    CHECK(!hal::expander::get(S::PdPg).v);  // asserted == low
    CHECK(hal::power::read().v.plugged);
    sim::set_plugged(false);
    CHECK(hal::expander::get(S::PdPg).v);
    CHECK(!hal::power::read().v.plugged);

    // FAULT idles high and the driver reports it as no fault.  (Nothing in the fake asserts it
    // yet; what is being pinned here is the inversion, which is the half that gets written
    // backwards.)
    CHECK(hal::expander::get(S::Fault).v);
    CHECK(!hal::power::read().v.fault);
}

// The divider's bottom leg is `Q3` and it is OPEN by default: 100k/100k across a backup cell is
// a permanent 20 uA drain otherwise (power.md).  So a read has to switch it in and put it back,
// and "put it back" is the half that gets forgotten -- a leg left in costs nothing visible and
// half the deep-sleep budget.
void test_reading_the_cell_leaves_the_divider_switched_out() {
    fresh();
    using S = hal::expander::Sig;
    CHECK(!hal::expander::get(S::VbatDivEn).v);  // parked open at power-on

    const uint32_t before = sim::vbat_div_reads();
    CHECK(hal::power::read().ok());
    CHECK(sim::vbat_div_reads() > before);       // it really did go through the leg
    CHECK(!hal::expander::get(S::VbatDivEn).v);  // ... and put it back
}

// The one thing on this board that can tell a full cell from an empty holder.  With `Q2`
// conducting its channel back-feeds the holder from the BAT node, so a plugged board reads
// "full" with nothing in the holder at all; `CELL_TEST` turns `Q2` off and the step at
// switch-off is the measurement (kicad/gen/b_charger.py).
void test_cell_test_tells_a_cell_from_an_empty_holder() {
    fresh();
    using S = hal::expander::Sig;
    sim::set_plugged(true);
    sim::set_vbat_mv(3700);

    auto t = hal::power::cell_test();
    CHECK(t.ok());
    CHECK(t.v.present);
    CHECK(t.v.step_mv == 0);  // the cell holds the holder+ net up: nothing moves
    // Left deasserted, and this is R-BOARD-2 rather than tidiness: a board unplugged while the
    // bit is up loses every rail and reboots for as long as it stays up.
    CHECK(!hal::expander::get(S::CellTest).v);

    // Take the cell out.  Plugged and un-tested it still reads "full" -- that is the failure
    // mode the discriminator exists for -- and the step at switch-off is one Q2 body diode.
    sim::set_cell_in(false);
    CHECK(hal::power::read().v.vbat_mv == hal::power::kVbatFullMv);
    t = hal::power::cell_test();
    CHECK(t.ok());
    CHECK(!t.v.present);
    CHECK(t.v.step_mv > 300);
    CHECK(!t.v.charging);  // an empty holder never charges; the LT3652 has nothing to push into
    CHECK(!hal::expander::get(S::CellTest).v);

    // R-BOARD-2 itself: on battery `Q2` off cuts all system power, and there is no hardware
    // interlock -- so the driver takes its own fresh PD_PG and refuses rather than trusting a
    // `plugged` somebody read a hundred milliseconds ago.
    sim::set_cell_in(true);
    sim::set_plugged(false);
    CHECK(hal::power::cell_test().st == Status::Denied);
    CHECK(!hal::expander::get(S::CellTest).v);  // and it never went up
}

// The 4.05 V health cap is the float divider's, in hardware.  `FULLCHG_EN` switches `R16` in for
// a 4.20 V top-up, and off is not merely the default: the expander is hi-Z at POR and `R24`
// holds `Q1` off, so a board that has never been told anything enforces the cap.
void test_full_charge_is_off_until_asked() {
    fresh();
    CHECK(hal::power::full_charge().ok());
    CHECK(!hal::power::full_charge().v);
    CHECK(hal::power::set_full_charge(true) == Status::Ok);
    CHECK(hal::power::full_charge().v);
    CHECK(hal::power::set_full_charge(false) == Status::Ok);
    CHECK(!hal::power::full_charge().v);
}

// D16, and the order it comes out in.  No expander means no divider leg to switch, so there is
// no cell voltage to be had either -- and the answer is NotPresent on both backends because the
// shared implementation reads the status pins before it touches the ADC.
void test_power_is_not_present_without_the_expander() {
    fresh();
    board::set_present(board::Dev::Expander, false);
    hal::mcp23017::forget();
    CHECK(hal::power::read().st == Status::NotPresent);
    CHECK(hal::power::cell_test().st == Status::NotPresent);
    board::set_present(board::Dev::Expander, true);
    hal::mcp23017::forget();

    board::set_present(board::Dev::Vbat, false);
    CHECK(hal::power::read().st == Status::NotPresent);
    board::set_present(board::Dev::Vbat, true);
    CHECK(hal::power::read().ok());
}

void test_snapshot_does_not_consume_the_knob_delta() {
    fresh();
    sim::turn_counts(20);
    const auto s = sim::snapshot();
    CHECK(s.knob_count == 20);
    // If the bridge stole this, the `ui` AO would see a knob that only turns when nobody is
    // looking at it.
    CHECK(hal::knob::read().v.delta == 20);
}

void run_motor_tests() {
    test_axis_integrates_in_sim_time();
    test_axis_runs_both_ways_and_holds();
    test_axis_does_not_shed_fractions();
    test_standby_freezes_the_hands();
    test_adopt_renames_without_moving();
    test_presence_gates_the_movement();
    test_opto_follows_the_hands();
    test_opto_manual_override_wins();
    test_a_fast_sweep_can_alias_past_the_index();
    test_sim_mechanism_commands();
    test_radio_toggle_polarity();
    test_expander_inputs_are_read_only();
    test_power_says_which_node_it_measured();
    test_power_status_lines_are_active_low();
    test_reading_the_cell_leaves_the_divider_switched_out();
    test_cell_test_tells_a_cell_from_an_empty_holder();
    test_full_charge_is_off_until_asked();
    test_power_is_not_present_without_the_expander();
    test_snapshot_does_not_consume_the_knob_delta();
    sim::reset();
    cli::unsafe_set(false);
}
