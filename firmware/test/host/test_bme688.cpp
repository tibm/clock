// The BME688 driver, against the register model behind the fake bus.   [FIRMWARE.md §11.2]
//
// The round trip below is the whole reason this part is modelled.  The model holds a
// realistic calibration set and bisects its OWN transcription of the datasheet's compensation
// formulas to produce the raw ADC words; the driver reads those words, parses the same
// calibration out of the same register bytes, and compensates with ITS transcription.  Two
// independent readings of the same tables have to agree -- which is a real test of the
// coefficient signedness, of the h1/h2 nibble split, and of the 20-bit ADC assembly, none of
// which has any other way of being checked short of a climate chamber.
//
// It also, quietly, proves the forced-mode handshake: the model raises `new_data` only when
// something writes mode = 01 to ctrl_meas, so a driver that forgot to trigger a conversion
// would sit in its poll loop and come back NotReady rather than returning stale numbers.
#include "check.hpp"
#include "testutil.hpp"

#include "clk/board.hpp"
#include "clk/hal/bme688.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/models.hpp"
#include "clk/hal/host/sim.hpp"

using namespace clk;
namespace sim = hal::host;
namespace model = hal::host::model;

namespace {

void fresh() {
    sim::reset();
    hal::bme688::forget();
    cli::unsafe_set(true);
}

bool near_abs(float got, float want, float tol) { return got >= want - tol && got <= want + tol; }

// One scene in, one reading out, compared against the model's own idea of the scene rather
// than against numbers typed twice.
void check_scene(float t, float rh, float p, uint32_t gas) {
    sim::set_env(t, rh, p, gas);

    float st = 0.0f, srh = 0.0f, sp = 0.0f;
    uint32_t sgas = 0;
    model::env_scene(st, srh, sp, sgas);
    CHECK(st == t);

    const auto s = hal::env::read();
    CHECK(s.ok());
    if (!s.ok()) return;
    CHECK(near_abs(s.v.temp_c, st, 0.1f));
    CHECK(near_abs(s.v.rh_pct, srh, 0.3f));
    CHECK(near_abs(s.v.press_hpa, sp, 0.2f));
    // Gas is quantised by a 10-bit ADC inside a range decade, so a percentage tolerance is
    // the only honest one -- and it has to hold at both ends, because picking the range code
    // is the part of the conversion the driver could get wrong.
    const double lo = static_cast<double>(sgas) * 0.98;
    const double hi = static_cast<double>(sgas) * 1.02;
    CHECK(static_cast<double>(s.v.gas_ohms) >= lo && static_cast<double>(s.v.gas_ohms) <= hi);
}

void test_compensation_round_trips() {
    fresh();
    check_scene(21.5f, 44.0f, 1013.2f, 120000);  // the default: an ordinary bedroom
    check_scene(4.0f, 90.0f, 978.5f, 45000);     // cold, damp, a low-pressure front
    check_scene(31.5f, 22.0f, 1042.0f, 380000);  // warm, dry, a high
    check_scene(-5.0f, 15.0f, 1100.0f, 900000);  // outside the product's range on purpose:
    check_scene(40.0f, 95.0f, 870.0f, 8000);     // the formulas must not fall apart at the ends
}

void test_gas_flags_reach_the_caller() {
    fresh();
    const auto s = hal::env::read();
    CHECK(s.ok());
    // The model always finishes its heater soak, so both flags are set.  What is being tested
    // is that they are DECODED -- gas_valid_r is bit 5 and heat_stab_r bit 4 of gas_r_lsb, one
    // bit apart, and swapping them is invisible until a real heater fails to settle.
    CHECK(s.v.gas_valid);
    CHECK(s.v.heat_stable);
}

void test_gas_range_code_is_used() {
    fresh();
    // Four resistances two decades apart force the model onto different gas_range codes, and
    // the driver's `262144 >> gas_range` is the only thing that can recover them.  A driver
    // that ignored the range would read one of these correctly and the rest by a power of two.
    const uint32_t decades[] = {6000u, 60000u, 600000u, 3000000u};
    for (const uint32_t want : decades) {
        sim::set_env(21.5f, 44.0f, 1013.2f, want);
        const auto s = hal::env::read();
        CHECK(s.ok());
        if (!s.ok()) continue;
        const double got = static_cast<double>(s.v.gas_ohms);
        CHECK(got >= want * 0.97 && got <= want * 1.03);
    }
}

void test_presence_and_cli() {
    fresh();
    sim::set_env(19.0f, 51.0f, 1008.0f, 88000);

    RecordingSink r;
    CHECK(run("sensor env read", r) == Status::Ok);
    CHECK(r.contains("t=19.0"));
    CHECK(r.contains("gas="));
    CHECK(r.contains("hPa"));

    // An unplugged daughterboard is NotPresent -- never an error, never a faked reading.
    board::set_present(board::Dev::Env, false);
    hal::bme688::forget();
    CHECK(hal::env::read().st == Status::NotPresent);
    RecordingSink a;
    CHECK(run("sensor env read", a) == Status::NotPresent);
    CHECK(a.contains("not present"));
}

void test_wrong_chip_id_is_refused() {
    fresh();
    // Something that ACKs at 0x77 and is not a BME688.  On a bus whose probe false-ACKs about
    // once in five hundred (§12.0.4) this is not hypothetical, and the driver must say so
    // rather than parse another part's registers as calibration and report the arithmetic.
    RecordingSink w;
    CHECK(run("board i2c write 0x77 0xD0 0x00", w) == Status::Ok);
    hal::bme688::forget();
    CHECK(hal::bme688::init() == Status::Failed);
    CHECK(hal::env::read().st == Status::Failed);

    sim::reset();  // put the chip ID back before anything else runs
    hal::bme688::forget();
    CHECK(hal::env::read().ok());
}

}  // namespace

void run_bme688_tests() {
    test_compensation_round_trips();
    test_gas_flags_reach_the_caller();
    test_gas_range_code_is_used();
    test_presence_and_cli();
    test_wrong_chip_id_is_refused();
    sim::reset();
    hal::bme688::forget();
    cli::unsafe_set(false);
}
