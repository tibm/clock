// The BNO085 driver's testable half.                      [FIRMWARE.md §6.5.1, §13.9 item 9]
//
// There is deliberately no host SHTP responder: §13.9 item 9 rules one out as a week of work
// to test code that only real silicon can invalidate, so on the host `hal::imu` stays the
// angle-based fake and this driver's transport is exercised on the bench.  What IS testable
// here is everything the transport hands to: the header parse, the fixed-point conversion,
// the axis map -- and, because the fake bus ACKs 0x4A and says nothing, the whole of
// R-BOARD-3's degraded path.
#include "check.hpp"
#include "testutil.hpp"

#include "clk/board.hpp"
#include "clk/domain/level.hpp"
#include "clk/hal/bno085.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"

using namespace clk;
namespace sim = hal::host;
namespace bno = hal::bno085;

namespace {

void fresh() {
    sim::reset();
    bno::forget();
    cli::unsafe_set(true);
}

bool near_f(float a, float b, float tol) { return a >= b - tol && a <= b + tol; }

void test_header_parse() {
    bno::Header h{};

    // The ordinary case, from the datasheet's own worked example (Fig. 5-2): a 19-byte cargo
    // on channel 3 carrying a timebase plus one report.  19 includes the header, so the
    // payload is 15.
    const uint8_t normal[4] = {0x13, 0x00, 0x03, 0x05};
    CHECK(bno::parse_header(normal, h));
    CHECK(h.len == 15);
    CHECK(h.chan == 3);
    CHECK(h.seq == 5);
    CHECK(!h.cont);

    // Bit 15 of the length is the continuation flag, not part of the length.  A driver that
    // took the raw 16 bits would read 0x8013 = 32787 bytes and wander off the end of the bus.
    const uint8_t cont[4] = {0x13, 0x80, 0x03, 0x05};
    CHECK(bno::parse_header(cont, h));
    CHECK(h.len == 15);
    CHECK(h.cont);

    // Length zero is the idle answer and by far the most common one -- "nothing pending", not
    // an error.  Anything under the four header bytes is the same thing.
    const uint8_t idle[4] = {0x00, 0x00, 0x00, 0x00};
    CHECK(!bno::parse_header(idle, h));
    const uint8_t runt[4] = {0x03, 0x00, 0x03, 0x00};
    CHECK(!bno::parse_header(runt, h));

    // Exactly four: a real cargo with an empty payload.
    const uint8_t empty[4] = {0x04, 0x00, 0x02, 0x07};
    CHECK(bno::parse_header(empty, h));
    CHECK(h.len == 0);
    CHECK(h.chan == 2);

    // 0xFFFF is reserved *because* a failed peripheral produces it so easily (§1.3.1).
    const uint8_t stuck[4] = {0xFF, 0xFF, 0x00, 0x00};
    CHECK(!bno::parse_header(stuck, h));

    // The hub has six channels; anything else is a bus that is not talking SHTP at us.
    const uint8_t bad_chan[4] = {0x08, 0x00, 0x06, 0x00};
    CHECK(!bno::parse_header(bad_chan, h));

    // Byte ORDER, which is the failure this test exists for: 0x0100 = 256 little-endian.
    // Read big-endian the same bytes are 0x0001, which is under the header size and would be
    // rejected -- so getting this backwards presents as "the hub never has anything to say".
    const uint8_t order[4] = {0x00, 0x01, 0x03, 0x00};
    CHECK(bno::parse_header(order, h));
    CHECK(h.len == 252);
}

void test_fixed_point() {
    // Gravity is Q8: the int16 is in 1/256ths of a metre per second squared.
    CHECK(bno::from_q(256, 8) == 1.0f);
    CHECK(bno::from_q(-256, 8) == -1.0f);
    CHECK(bno::from_q(0, 8) == 0.0f);
    CHECK(near_f(bno::from_q(32767, 8), 127.996f, 0.001f));
    CHECK(bno::from_q(-32768, 8) == -128.0f);
    // One g, as the hub would actually send it.
    CHECK(near_f(bno::from_q(2510, 8), 9.8047f, 0.001f));
}

void test_dial_axis_map_is_the_bench_procedure() {
    // These four poses ARE the procedure in the driver's comment, written down where they can
    // fail.  If the daughterboard turns out to be mounted rotated, to_dial_axes() changes and
    // these expectations do not -- they are the definition of the dial frame, not a record of
    // one wiring.
    constexpr float g = 9.80665f;
    float x = 0.0f, y = 0.0f, z = 0.0f;

    bno::to_dial_axes(0.0f, -g, 0.0f, x, y, z);  // upright, facing you
    CHECK(near_f(domain::up_deg(x, y), 0.0f, 0.5f));

    bno::to_dial_axes(g, 0.0f, 0.0f, x, y, z);  // laid on its right-hand face
    CHECK(near_f(domain::up_deg(x, y), 270.0f, 0.5f));

    bno::to_dial_axes(-g, 0.0f, 0.0f, x, y, z);  // laid on its left-hand face
    CHECK(near_f(domain::up_deg(x, y), 90.0f, 0.5f));

    bno::to_dial_axes(0.0f, g, 0.0f, x, y, z);  // upside down
    CHECK(near_f(domain::up_deg(x, y), 180.0f, 0.5f));

    // Dial to the ceiling: all of gravity is out through the glass and nothing in the dial
    // plane points up.  §6.1d's dead zone is what that case is for, and the property the dead
    // zone relies on is that the in-plane magnitude collapses.
    bno::to_dial_axes(0.0f, 0.0f, -g, x, y, z);
    CHECK(near_f(x * x + y * y, 0.0f, 0.01f));
}

void test_silent_hub_degrades_and_backs_off() {
    fresh();
    // The fake bus ACKs 0x4A and answers zeros -- which is exactly what a wedged BNO085 looks
    // like from the master's side, and precisely the case R-BOARD-3 says can never be cleared
    // by firmware.  The handshake must end in a verdict, not a wait.
    CHECK(bno::init() == Status::Failed);
    CHECK(!bno::link().ready);
    CHECK(bno::link().last_ms_ago == hal::imu::kNever);

    // ...and the next attempt must be refused cheaply rather than repeating the whole
    // several-hundred-millisecond boot drain.  A dead hub does not get to decide how often
    // the expander and the amp reach the bus.
    CHECK(bno::init() == Status::NotReady);
    CHECK(bno::read().st == Status::NotReady);

    // Absent is a different answer from silent, and the two must not be conflated: one means
    // the daughterboard is unplugged, the other means it is plugged in and not talking.
    board::set_present(board::Dev::Imu, false);
    bno::forget();
    CHECK(bno::init() == Status::NotPresent);
    CHECK(bno::read().st == Status::NotPresent);
}

void test_host_imu_surface() {
    fresh();
    // The fake, not the driver -- but `sensor imu` and the `ui` AO go through this surface on
    // both platforms, so its shape is worth pinning.
    sim::set_orientation(90.0f, 0.0f, 0.0f);
    const auto s = hal::imu::read();
    CHECK(s.ok());
    CHECK(near_f(domain::up_deg(s.v.gx, s.v.gy), 270.0f, 1.0f));
    CHECK(hal::imu::link().ready);

    const uint16_t before = s.v.taps;
    sim::tap();
    sim::tap();
    const auto after = hal::imu::read();
    CHECK(after.ok());
    CHECK(static_cast<uint16_t>(after.v.taps - before) == 2);

    RecordingSink r;
    CHECK(run("sensor imu read", r) == Status::Ok);
    CHECK(r.contains("g="));
    CHECK(r.contains("up="));
    CHECK(r.contains("taps="));
    CHECK(r.contains("pkt="));

    // R-BOARD-3's degraded path as the CLI shows it.
    board::set_present(board::Dev::Imu, false);
    CHECK(hal::imu::read().st == Status::NotPresent);
    CHECK(!hal::imu::link().ready);
    CHECK(hal::imu::link().last_ms_ago == hal::imu::kNever);
    RecordingSink a;
    CHECK(run("sensor imu read", a) == Status::NotPresent);
}

}  // namespace

void run_bno085_tests() {
    test_header_parse();
    test_fixed_point();
    test_dial_axis_map_is_the_bench_procedure();
    test_silent_hub_degrades_and_backs_off();
    test_host_imu_surface();
    sim::reset();
    bno::forget();
    cli::unsafe_set(false);
}
