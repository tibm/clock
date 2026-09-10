// The TSL2591 driver, against the register model behind the fake bus.  [FIRMWARE.md §11.2]
//
// This is the shipping driver under test, not a stand-in for it: `hal::als::read()` here runs
// the same tsl2591.cpp the ESP32 links.  What the model gives back is derived from a SCENE in
// lux through whatever gain the driver actually programmed, so the round-trip below is a real
// test of the auto-range and of the lux fit -- the two things that have no other way of being
// checked without a lamp, a reference meter and a dark room.
#include "check.hpp"
#include "testutil.hpp"

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/hal/tsl2591.hpp"

using namespace clk;
namespace sim = hal::host;

namespace {

void fresh() {
    sim::reset();
    hal::tsl2591::forget();
    cli::unsafe_set(true);
}

bool within_pct(float got, float want, float pct) {
    const float tol = want * pct / 100.0f;
    return got >= want - tol && got <= want + tol;
}

// ---- the pure half ----------------------------------------------------------------------

void test_full_scale_is_the_datasheets() {
    // 37888, not the 36863 that half the drivers on the internet use.  The difference is a
    // thousand counts of headroom at the top of the 100 ms range, which is exactly where the
    // auto-range makes its decisions.
    CHECK(hal::tsl2591::full_scale(100) == 37888);
    CHECK(hal::tsl2591::full_scale(200) == 65535);
    CHECK(hal::tsl2591::full_scale(600) == 65535);
}

void test_lux_fit_edges() {
    using hal::tsl2591::lux_from_counts;

    // Dark is a reading.  It is not a failure to fit, and reporting -1 for an unlit room
    // would make every night-time sample look like a fault.
    CHECK(lux_from_counts(0, 0, 1, 100) == 0.0f);

    // Saturation on either channel: the true level is somewhere above this, so no number
    // would be honest.
    CHECK(lux_from_counts(37888, 100, 1, 100) < 0.0f);
    CHECK(lux_from_counts(1000, 65535, 1, 200) < 0.0f);
    // ...and the same counts are FINE at a longer integration, because full scale moved.
    CHECK(lux_from_counts(37888, 1000, 1, 200) > 0.0f);

    // More infrared than full spectrum is physically impossible -- optics or wiring, not a
    // light level.  Clamped to zero rather than allowed to go negative.
    CHECK(lux_from_counts(500, 900, 1, 100) == 0.0f);
    CHECK(lux_from_counts(500, 500, 1, 100) == 0.0f);

    // Nonsense parameters, which is a caller bug rather than a reading.
    CHECK(lux_from_counts(1000, 100, 0, 100) < 0.0f);
    CHECK(lux_from_counts(1000, 100, 1, 0) < 0.0f);

    // A worked value: CPL = 100 * 1 / 408, lux = (1000-100) * (1 - 0.1) / CPL = 3304.8
    CHECK(within_pct(lux_from_counts(1000, 100, 1, 100), 3304.8f, 1.0f));
    // Gain divides it exactly: the same counts at 25x are the same room 25x darker.
    CHECK(within_pct(lux_from_counts(1000, 100, 25, 100), 3304.8f / 25.0f, 1.0f));
    // Integration time does the same, and the pair is what makes the ladder work at all.
    CHECK(within_pct(lux_from_counts(1000, 100, 1, 200), 3304.8f / 2.0f, 1.0f));

    // Monotone in ch0, which is the property the auto-range's "too bright / too dim" test
    // silently assumes.
    CHECK(lux_from_counts(2000, 200, 1, 100) > lux_from_counts(1000, 100, 1, 100));
}

// ---- the driver, through the bus --------------------------------------------------------

void test_auto_range_spans_the_decades() {
    fresh();
    // A bedside clock sees roughly six decades between noon at a window and 3 a.m., and the
    // gain ladder is the only thing that covers it.  Each of these lands on a different rung.
    const float scenes[] = {0.5f, 12.0f, 50.0f, 800.0f, 5000.0f, 40000.0f};
    for (const float want : scenes) {
        sim::set_lux(want);
        const auto s = hal::als::read();
        CHECK(s.ok());
        if (!s.ok()) continue;
        CHECK(!s.v.saturated);
        CHECK(within_pct(s.v.lux, want, 2.0f));
        // In band: the whole point of re-ranging is that neither end of the ADC is wasted.
        CHECK(s.v.ch0 > 0);
        CHECK(s.v.ch0 < hal::tsl2591::full_scale(s.v.integ_ms));
        // And the driver reports the rung it actually settled on, not the one it wanted.
        CHECK(s.v.gain_x == 1 || s.v.gain_x == 25 || s.v.gain_x == 428 || s.v.gain_x == 9876);
        CHECK(s.v.integ_ms == 100);
    }
}

void test_bright_light_saturates_honestly() {
    fresh();
    // Past the bottom of the ladder there is nowhere left to go, and the answer must be "I
    // cannot see this", not a number.  A driver that quietly reported the clipped counts
    // would read 100 klux for every value above it.
    sim::set_lux(300000.0f);
    const auto s = hal::als::read();
    CHECK(s.ok());
    CHECK(s.v.saturated);
    CHECK(s.v.lux < 0.0f);
    CHECK(s.v.gain_x == 1);  // it did walk all the way down first
}

void test_darkness_reads_zero_not_an_error() {
    fresh();
    sim::set_lux(0.0f);
    const auto s = hal::als::read();
    CHECK(s.ok());
    CHECK(!s.v.saturated);
    CHECK(s.v.lux == 0.0f);
    CHECK(s.v.gain_x == 9876);  // climbed to the top looking for something
}

void test_command_byte_is_mandatory() {
    fresh();
    // The model NACKs an access with no COMMAND bit, exactly as the part does.  This is the
    // single most common way a TSL2591 appears dead, so it is worth one test that says so:
    // 0x12 is the ID register, and only 0xA0|0x12 reaches it.
    RecordingSink raw;
    CHECK(run("board i2c read 0x29 0x12", raw) == Status::NotPresent);

    RecordingSink cmd;
    CHECK(run("board i2c read 0x29 0xB2", cmd) == Status::Ok);
    CHECK(cmd.contains("0x50"));
}

void test_presence_and_cli() {
    fresh();
    sim::set_lux(300.0f);

    RecordingSink r;
    CHECK(run("sensor als read", r) == Status::Ok);
    CHECK(r.contains("lux="));
    CHECK(r.contains("gain="));
    CHECK(r.contains("int="));

    // An unplugged daughterboard is NotPresent, never an error and never a faked reading.
    board::set_present(board::Dev::Als, false);
    hal::tsl2591::forget();
    CHECK(hal::als::read().st == Status::NotPresent);
    RecordingSink a;
    CHECK(run("sensor als read", a) == Status::NotPresent);
    CHECK(a.contains("not present"));
}

}  // namespace

void run_tsl2591_tests() {
    test_full_scale_is_the_datasheets();
    test_lux_fit_edges();
    test_auto_range_spans_the_decades();
    test_bright_light_saturates_honestly();
    test_darkness_reads_zero_not_an_error();
    test_command_byte_is_mandatory();
    test_presence_and_cli();
    sim::reset();
    hal::tsl2591::forget();
    cli::unsafe_set(false);
}
