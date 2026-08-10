// The fake HAL and the `sim` group.                        [FIRMWARE.md §11.1, §11.2, D14]
#include "check.hpp"
#include "testutil.hpp"

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"

using namespace clk;
namespace sim = hal::host;

namespace {

void fresh() {
    sim::reset();
    cli::unsafe_set(true);        // most of ui/ is gated; the gate itself is tested in test_cli
}

}  // namespace

// ---- the fakes themselves ---------------------------------------------------------------

void test_opto() {
    fresh();
    sim::set_opto(0.0f);
    auto mv = hal::adc::read_mv(hal::adc::Ch::Opto);
    CHECK(mv.ok() && mv.v == 200);                    // dark end of the calibration span

    sim::set_opto(1.0f);
    mv = hal::adc::read_mv(hal::adc::Ch::Opto);
    CHECK(mv.ok() && mv.v == 3000);

    sim::set_opto(0.5f);
    const auto n = hal::adc::read_opto_norm();
    CHECK(n.ok() && n.v > 0.49f && n.v < 0.51f);

    sim::set_opto(5.0f);                              // clamped, not rejected
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
    CHECK(r.st == Status::NotPresent);                // D16: absence is not an error

    board::set_present(board::Dev::Opto, true);
    CHECK(hal::adc::read_mv(hal::adc::Ch::Opto).ok());
}

void test_knob() {
    fresh();
    auto k = hal::knob::read();
    CHECK(k.ok() && k.v.count == 0 && !k.v.sw);

    sim::turn(+5);
    k = hal::knob::read();
    CHECK(k.v.count == 20);                           // 4 PCNT counts per detent
    CHECK(k.v.delta == 20);

    k = hal::knob::read();
    CHECK(k.v.delta == 0);                            // delta is since the last read

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
    CHECK(!sim::refreshed());                         // nothing lights until refresh()
    CHECK(hal::pixels::refresh() == Status::Ok);
    CHECK(sim::refreshed());

    CHECK(hal::pixels::set(hal::pixels::kCount, {}) == Status::BadArg);   // 7 pixels, 0..6

    hal::pixels::set_all({});
    hal::pixels::set(2, {255, 0, 0, 0});
    char buf[16];
    sim::render_pixels(buf, sizeof buf);
    CHECK_STREQ(buf, "..R....");                      // bell is chain position 3 = index 2
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
    CHECK(hal::wake::set(0, 0) == Status::Ok);        // turning it off is always allowed
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
    CHECK(hal::clock_::micros() >= t1);               // re-basing never goes backwards

    sim::set_warp(1e9);                               // clamped
    CHECK(sim::warp() <= 10000.0);
}

void test_i2c_scan_follows_presence() {
    fresh();
    uint8_t addr[8];
    auto r = hal::i2c::scan(addr, sizeof addr);
    CHECK(r.ok() && r.v == 5);                        // expander, als, imu, amp, env

    board::set_present(board::Dev::Imu, false);
    r = hal::i2c::scan(addr, sizeof addr);
    CHECK(r.v == 4);
    CHECK(hal::i2c::read_reg(0x4A, 0).st == Status::NotPresent);
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
    CHECK(l.contains("no-drv"));                      // als/env/imu: fitted, no driver yet

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
    CHECK(s.lines.size() > 5);                        // header + samples + summary

    RecordingSink csv;
    run("sensor homing stream 20 1 --csv", csv);
    CHECK(csv.contains("# t_ms,mv,norm"));

    RecordingSink over;
    run("sensor homing stream 9999 1", over);         // clamped to the sensor's max_hz
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
    CHECK(hal::pixels::get(2).r == 255);              // bell untouched

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
    test_sim_commands();
    test_sensor_grammar();
    test_sensor_stream_is_bounded();
    test_ui_led();
    test_ui_wake_gate();
    sim::reset();
    cli::unsafe_set(false);
}
