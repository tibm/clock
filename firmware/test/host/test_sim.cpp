// The fake HAL and the `sim` group.                        [FIRMWARE.md §11.1, §11.2, D14]
#include "check.hpp"
#include "testutil.hpp"

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/hal/mcp23017.hpp"

using namespace clk;
namespace sim = hal::host;

namespace {

void fresh() {
    sim::reset();
    cli::unsafe_set(true);  // most of ui/ is gated; the gate itself is tested in test_cli
}

}  // namespace

// ---- the fakes themselves ---------------------------------------------------------------

void test_opto() {
    fresh();
    sim::set_opto(0.0f);
    auto mv = hal::adc::read_mv(hal::adc::Ch::Opto);
    CHECK(mv.ok() && mv.v == 200);  // dark end of the calibration span

    sim::set_opto(1.0f);
    mv = hal::adc::read_mv(hal::adc::Ch::Opto);
    CHECK(mv.ok() && mv.v == 3000);

    sim::set_opto(0.5f);
    const auto n = hal::adc::read_opto_norm();
    CHECK(n.ok() && n.v > 0.49f && n.v < 0.51f);

    sim::set_opto(5.0f);  // clamped, not rejected
    CHECK(sim::opto() == 1.0f);
    sim::set_opto(-1.0f);
    CHECK(sim::opto() == 0.0f);
}

void test_noise_is_deterministic() {
    fresh();
    sim::set_opto(0.5f);
    sim::set_noise_mv(50);

    sim::set_seed(42);
    uint16_t a[6];
    for (auto& v : a) v = hal::adc::read_mv(hal::adc::Ch::Opto).v;

    sim::set_seed(42);
    for (auto& expect : a) CHECK(hal::adc::read_mv(hal::adc::Ch::Opto).v == expect);

    // and it actually varies -- a "deterministic" generator that returns a constant would
    // pass the round-trip above while testing nothing
    bool varied = false;
    for (std::size_t i = 1; i < 6; ++i) varied = varied || (a[i] != a[0]);
    CHECK(varied);
}

void test_presence_gates_reads() {
    fresh();
    CHECK(hal::adc::read_mv(hal::adc::Ch::Opto).ok());

    board::set_present(board::Dev::Opto, false);
    const auto r = hal::adc::read_mv(hal::adc::Ch::Opto);
    CHECK(!r.ok());
    CHECK(r.st == Status::NotPresent);  // D16: absence is not an error

    board::set_present(board::Dev::Opto, true);
    CHECK(hal::adc::read_mv(hal::adc::Ch::Opto).ok());
}

void test_knob() {
    fresh();
    auto k = hal::knob::read();
    CHECK(k.ok() && k.v.count == 0 && !k.v.sw);

    sim::turn(+5);
    k = hal::knob::read();
    CHECK(k.v.count == 20);  // 4 PCNT counts per detent
    CHECK(k.v.delta == 20);

    k = hal::knob::read();
    CHECK(k.v.delta == 0);  // delta is since the last read

    sim::turn(-3);
    k = hal::knob::read();
    CHECK(k.v.count == 8 && k.v.delta == -12);

    sim::press(60'000);
    CHECK(hal::knob::read().v.sw);
    sim::press(0);
    CHECK(!hal::knob::read().v.sw);
}

void test_pixels() {
    fresh();
    CHECK(hal::pixels::set(0, {1, 2, 3, 4}) == Status::Ok);
    CHECK(hal::pixels::get(0) == hal::pixels::Rgbw{1, 2, 3, 4});
    CHECK(!sim::refreshed());  // nothing lights until refresh()
    CHECK(hal::pixels::refresh() == Status::Ok);
    CHECK(sim::refreshed());

    CHECK(hal::pixels::set(hal::pixels::kCount, {}) == Status::BadArg);  // 7 pixels, 0..6

    hal::pixels::set_all({});
    hal::pixels::set(2, {255, 0, 0, 0});
    char buf[16];
    sim::render_pixels(buf, sizeof buf);
    CHECK_STREQ(buf, "..R....");  // bell is chain position 3 = index 2
}

void test_wake_is_plugged_only() {
    fresh();
    CHECK(sim::plugged());
    CHECK(hal::wake::set(40, 10) == Status::Ok);
    CHECK(hal::wake::warm() == 40 && hal::wake::cool() == 10);

    // §6.8 interlock 4 / §7.4: the 12 V boost is plugged-only, so the fake refuses on
    // battery.  A service that forgets the gate fails here rather than on a bench.
    sim::set_plugged(false);
    CHECK(hal::wake::set(40, 10) == Status::Denied);
    CHECK(hal::wake::set(0, 0) == Status::Ok);  // turning it off is always allowed
    CHECK(hal::wake::set(101, 0) == Status::BadArg);
}

void test_time() {
    fresh();
    const uint64_t t0 = hal::clock_::micros();
    sim::advance(5'000'000);
    const uint64_t t1 = hal::clock_::micros();
    CHECK(t1 >= t0 + 5'000'000);

    sim::set_warp(60.0);
    CHECK(sim::warp() == 60.0);
    CHECK(hal::clock_::micros() >= t1);  // re-basing never goes backwards

    sim::set_warp(1e9);  // clamped
    CHECK(sim::warp() <= 10000.0);
}

void test_i2c_scan_follows_presence() {
    fresh();
    uint8_t addr[8];
    auto r = hal::i2c::scan(addr, sizeof addr);
    CHECK(r.ok() && r.v == 5);  // expander, als, imu, amp, env

    board::set_present(board::Dev::Imu, false);
    r = hal::i2c::scan(addr, sizeof addr);
    CHECK(r.v == 4);
    CHECK(hal::i2c::read_reg(0x4A, 0).st == Status::NotPresent);
}

// The MCP23017 at register level, which exists so the driver that replaces
// `hal::expander`'s named signals can be written and tested with no board (§11.2, §13.9-9).
// The property that matters is that the two views of the expander are ONE device: what the
// named surface sets, the register surface reads, and the other way round.
void test_i2c_expander_register_view() {
    using E = hal::expander::Sig;
    constexpr uint8_t kAddr = 0x20, kIodirA = 0x00, kGpioA = 0x12, kOlatA = 0x14, kGppuB = 0x0D;
    fresh();

    // POR: both IODIR all-inputs.  A driver that assumes otherwise is a driver that will
    // drive nothing on the bench and be very hard to explain.
    CHECK(hal::i2c::read_reg(kAddr, kIodirA).v == 0xFF);

    // Named -> registers.  SPK_SD is GPA0.
    CHECK(hal::expander::set(E::SpkSd, true) == Status::Ok);
    CHECK((hal::i2c::read_reg(kAddr, kGpioA).v & 0x01) == 0x01);
    CHECK(hal::expander::set(E::SpkSd, false) == Status::Ok);
    CHECK((hal::i2c::read_reg(kAddr, kGpioA).v & 0x01) == 0x00);

    // Registers -> named, but ONLY once the pin is configured as an output.  Forgetting
    // IODIR is the classic MCP23017 bug and the fake has to reproduce it, not paper over it.
    CHECK(hal::i2c::write_reg(kAddr, kOlatA, 0x01) == Status::Ok);
    CHECK(hal::expander::get(E::SpkSd).v == false);  // still an input: the write went nowhere
    CHECK(hal::i2c::write_reg(kAddr, kIodirA, 0xFE) == Status::Ok);  // GPA0 out
    CHECK(hal::i2c::write_reg(kAddr, kOlatA, 0x01) == Status::Ok);
    CHECK(hal::expander::get(E::SpkSd).v == true);

    // An input pin stays the outside world's to drive, whatever IODIR says.  RADIO_OFF is
    // GPA3 and a write must not move it.
    const bool radio = hal::expander::get(E::RadioOff).v;
    CHECK(hal::i2c::write_reg(kAddr, kIodirA, 0x00) == Status::Ok);  // claim ALL of port A
    CHECK(hal::i2c::write_reg(kAddr, kOlatA, static_cast<uint8_t>(radio ? 0x00 : 0x08)) ==
          Status::Ok);
    CHECK(hal::expander::get(E::RadioOff).v == radio);

    // R-BOARD-4's register is real and readable back, so the `board` AO's GPPU.3 write can
    // be asserted rather than assumed.
    CHECK(hal::i2c::write_reg(kAddr, kGppuB, 0x08) == Status::Ok);
    CHECK(hal::i2c::read_reg(kAddr, kGppuB).v == 0x08);

    // And the whole device disappears with the presence flag, registers included.
    board::set_present(board::Dev::Expander, false);
    CHECK(hal::i2c::read_reg(kAddr, kGpioA).st == Status::NotPresent);
    CHECK(hal::expander::get(E::SpkSd).st == Status::NotPresent);
    board::set_present(board::Dev::Expander, true);
}

// The REAL MCP23017 driver, run against the register-level fake.  This is the pay-off for
// modelling registers rather than named signals (§11.2): the code under test here is the code
// that ships, and it is the config sequence -- the part that has hardware requirements
// attached to it -- rather than anything the fake invents.
void test_mcp23017_driver_configures_the_chip() {
    using E = hal::expander::Sig;
    constexpr uint8_t A = hal::mcp23017::kAddr;
    constexpr uint8_t IOCON = 0x0A, IODIRA = 0x00, IODIRB = 0x01, GPPUA = 0x0C, GPPUB = 0x0D;
    fresh();
    hal::mcp23017::forget();

    CHECK(hal::mcp23017::init() == Status::Ok);

    // R-BOARD-1: INTA/INTB are tied on the board, so MIRROR must be set or two push-pull
    // outputs contend the moment per-bank interrupts are enabled.
    CHECK((hal::i2c::read_reg(A, IOCON).v & 0x40) == 0x40);

    // Directions match the port map: GPA0-2 out, GPB4/5/7 out, everything else in.
    CHECK(hal::i2c::read_reg(A, IODIRA).v == 0xF8);
    CHECK(hal::i2c::read_reg(A, IODIRB).v == 0x4F);

    // R-BOARD-4: GPB3 (ALS_INT) must have the internal pull-up, because its only other one
    // lives on a daughterboard that is regularly unplugged.
    CHECK((hal::i2c::read_reg(A, GPPUB).v & 0x08) == 0x08);
    CHECK(hal::i2c::read_reg(A, GPPUA).v == 0xF8);  // and every other input, for the same reason

    // Outputs park LOW, which is the idle-safe state for every one of them: amp muted, coils
    // dead, 12 V gate shut, CELL_TEST unasserted (R-BOARD-2).
    for (auto s : {E::SpkSd, E::StepStby, E::Boost12En, E::FullchgEn, E::VbatDivEn, E::CellTest}) {
        const auto v = hal::mcp23017::get(s);
        CHECK(v.ok() && v.v == false);
    }

    // A round trip through the driver reaches the pin and reads back off GPIO, not off the
    // shadow -- so a latch that never made it to the chip would fail here.
    CHECK(hal::mcp23017::set(E::Boost12En, true) == Status::Ok);
    CHECK(hal::mcp23017::get(E::Boost12En).v == true);
    CHECK(hal::mcp23017::set(E::Boost12En, false) == Status::Ok);
    CHECK(hal::mcp23017::get(E::Boost12En).v == false);

    // Inputs are the outside world's.
    CHECK(hal::mcp23017::set(E::RadioOff, true) == Status::BadArg);
    CHECK(hal::mcp23017::set(E::PdPg, false) == Status::BadArg);

    // And the whole driver answers NotPresent when the chip does not, rather than pretending.
    hal::mcp23017::forget();
    board::set_present(board::Dev::Expander, false);
    CHECK(hal::mcp23017::init() == Status::NotPresent);
    CHECK(hal::mcp23017::get(E::SpkSd).st == Status::NotPresent);
    CHECK(hal::mcp23017::set(E::SpkSd, true) == Status::NotPresent);
    board::set_present(board::Dev::Expander, true);
    hal::mcp23017::forget();
}

// ---- the sim + sensor + ui commands on top ----------------------------------------------

void test_sim_commands() {
    fresh();
    RecordingSink s;
    CHECK(run("sim opto 0.42", s) == Status::Ok);
    CHECK(sim::opto() > 0.41f && sim::opto() < 0.43f);

    RecordingSink bad;
    CHECK(run("sim opto 2", bad) == Status::BadArg);
    CHECK(bad.contains("0..1"));

    RecordingSink t;
    CHECK(run("sim turn +3", t) == Status::Ok);
    CHECK(hal::knob::read().v.count == 12);

    RecordingSink p;
    run("sim unplug", p);
    CHECK(!sim::plugged());
    CHECK(p.contains("battery"));
    run("sim plug", p);
    CHECK(sim::plugged());

    RecordingSink pr;
    run("sim present opto off", pr);
    CHECK(!board::present(board::Dev::Opto));
    run("sim present opto on", pr);
    CHECK(board::present(board::Dev::Opto));
}

void test_sensor_grammar() {
    fresh();
    sim::set_opto(0.25f);

    // The wildcard object: `sensor <name> <verb>`, which is the form §9.5 specifies.
    RecordingSink r;
    CHECK(run("sensor homing read", r) == Status::Ok);
    CHECK(r.contains("norm=0.25"));

    RecordingSink l;
    CHECK(run("sensor list", l) == Status::Ok);
    CHECK(l.contains("homing"));
    CHECK(l.contains("no-drv"));  // als/env/imu: fitted, no driver yet

    // absent hardware and a missing driver must not read the same
    RecordingSink nodrv;
    CHECK(run("sensor als read", nodrv) == Status::NotReady);
    CHECK(nodrv.contains("no driver"));

    board::set_present(board::Dev::Opto, false);
    RecordingSink absent;
    CHECK(run("sensor homing read", absent) == Status::NotPresent);
    CHECK(absent.contains("not present"));

    RecordingSink nope;
    CHECK(run("sensor banana read", nope) == Status::BadArg);
    CHECK(nope.contains("no such sensor"));
}

void test_sensor_stream_is_bounded() {
    fresh();
    RecordingSink s;
    CHECK(run("sensor homing stream 20 1", s) == Status::Ok);
    CHECK(s.contains("stream ended"));
    CHECK(s.contains("0 dropped"));
    CHECK(s.lines.size() > 5);  // header + samples + summary

    RecordingSink csv;
    run("sensor homing stream 20 1 --csv", csv);
    CHECK(csv.contains("# t_ms,mv,norm"));

    RecordingSink over;
    run("sensor homing stream 9999 1", over);  // clamped to the sensor's max_hz
    CHECK(over.contains("tops out at 200"));

    RecordingSink bad;
    CHECK(run("sensor homing stream 10 9999", bad) == Status::BadArg);

    // an absent sensor costs one line, not a thread and a ten-second wait
    board::set_present(board::Dev::Opto, false);
    RecordingSink gone;
    CHECK(run("sensor homing stream 10 5", gone) == Status::NotPresent);
}

void test_ui_led() {
    fresh();
    RecordingSink s;
    CHECK(run("ui led bell red", s) == Status::Ok);
    CHECK(hal::pixels::get(2) == hal::pixels::Rgbw{255, 0, 0, 0});

    // `dial` is a range and it is pixels 0-1 (on-PCB), NOT 5-6.  README §9 had this
    // backwards; this test is the thing that keeps it fixed.
    run("ui led dial white", s);
    CHECK(hal::pixels::get(0).w == 255);
    CHECK(hal::pixels::get(1).w == 255);
    CHECK(hal::pixels::get(2).r == 255);  // bell untouched

    RecordingSink dim;
    run("ui led 3 red@20", dim);
    CHECK(hal::pixels::get(3) == hal::pixels::Rgbw{51, 0, 0, 0});

    RecordingSink hex;
    run("ui led 4 #ff8800", hex);
    CHECK(hal::pixels::get(4) == hal::pixels::Rgbw{0xff, 0x88, 0x00, 0});

    RecordingSink quad;
    run("ui led 5 1 2 3 4", quad);
    CHECK(hal::pixels::get(5) == hal::pixels::Rgbw{1, 2, 3, 4});

    RecordingSink off;
    run("ui led all off", off);
    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        CHECK(hal::pixels::get(i) == hal::pixels::Rgbw{});
    }

    RecordingSink badc;
    CHECK(run("ui led bell banana", badc) == Status::BadArg);
    CHECK(badc.contains("bad colour"));

    RecordingSink badid;
    CHECK(run("ui led 99 red", badid) == Status::BadArg);

    // `ui led test` has its own row and must not be eaten by the wildcard-ish `ui led <id>`
    RecordingSink walk;
    CHECK(run("ui led test", walk) == Status::Ok);
    CHECK(walk.contains("dial (on-PCB)"));
    CHECK(walk.contains("status (via J12)"));
}

void test_ui_wake_gate() {
    fresh();
    RecordingSink s;
    CHECK(run("ui wake 40 10", s) == Status::Ok);

    sim::set_plugged(false);
    RecordingSink d;
    CHECK(run("ui wake 40 10", d) == Status::Denied);
    CHECK(d.contains("plugged-only"));
}

void run_sim_tests() {
    test_opto();
    test_noise_is_deterministic();
    test_presence_gates_reads();
    test_knob();
    test_pixels();
    test_wake_is_plugged_only();
    test_time();
    test_i2c_scan_follows_presence();
    test_i2c_expander_register_view();
    test_mcp23017_driver_configures_the_chip();
    test_sim_commands();
    test_sensor_grammar();
    test_sensor_stream_is_bounded();
    test_ui_led();
    test_ui_wake_gate();
    sim::reset();
    cli::unsafe_set(false);
}
