// The dial finding up.                                        [FIRMWARE.md §6.1d, §11.1]
//
// All of it is domain/level.hpp, which is pure -- so a case here is a cube turned over, not
// a clock booted.  That is the point of the split: the rules that decide when a reading is
// worth half a turn of both hands can be tried ten thousand ways in a millisecond, and the
// AO test next door (test_motion.cpp) then only has to prove the answer reaches the hands.
#include <cmath>

#include "check.hpp"

#include "clk/domain/level.hpp"

using namespace clk;
using domain::kRev;

namespace {

constexpr float kG = 9.80665f;

// Gravity as the sensor would report it with the cube turned `yaw` degrees clockwise about
// the dial's own axis and tipped `pitch` degrees away from vertical.  The same model the
// fake HAL uses, written out again here on purpose: a test that shares the code under test's
// arithmetic proves the two agree, not that either is right.
struct G {
    float x, y, z;
};
G at(float yaw_deg, float pitch_deg = 0.0f) {
    const float y = yaw_deg * 3.14159265f / 180.0f, p = pitch_deg * 3.14159265f / 180.0f;
    return {kG * std::cos(p) * std::sin(y), -kG * std::cos(p) * std::cos(y), -kG * std::sin(p)};
}

// Settle the leveller on an orientation the way a poll would: repeatedly, until it stops
// changing its mind.  Returns the tick it ended on.
int settle(domain::Leveller& lv, G g, int polls = 6) {
    for (int i = 0; i < polls; ++i) lv.update(g.x, g.y, g.z);
    return lv.tick();
}

}  // namespace

// The geometry, before any of the policy: where "up" is on a dial that has been turned.
//
// Turn the cube 90 degrees clockwise and the printed 12 is now pointing at three o'clock,
// so up -- as the DIAL sees it -- is three quarters of the way round from the 12.  That is
// also exactly the offset the hands need: the dot at the top is the printed 9.
void test_level_up_is_where_the_dial_says() {
    CHECK(std::fabs(domain::up_deg(at(0).x, at(0).y) - 0.0f) < 0.01f);
    CHECK(std::fabs(domain::up_deg(at(90).x, at(90).y) - 270.0f) < 0.01f);
    CHECK(std::fabs(domain::up_deg(at(180).x, at(180).y) - 180.0f) < 0.01f);
    CHECK(std::fabs(domain::up_deg(at(270).x, at(270).y) - 90.0f) < 0.01f);
    // -30 and 330 are the same shelf.
    CHECK(std::fabs(domain::up_deg(at(-30).x, at(-30).y) - 30.0f) < 0.01f);

    // Twelve ticks, thirty degrees, and the arithmetic that turns one into microsteps.
    CHECK(domain::kTickUsteps == kRev / 12);
    CHECK(domain::nearest_tick(0.0f) == 0);
    CHECK(domain::nearest_tick(14.9f) == 0);
    CHECK(domain::nearest_tick(15.1f) == 1);
    CHECK(domain::nearest_tick(359.9f) == 0);  // and it wraps rather than reading 12
    CHECK(domain::tick_usteps(0) == 0);
    CHECK(domain::tick_usteps(3) == kRev / 4);
    CHECK(domain::tick_usteps(12) == 0);
    CHECK(domain::tick_usteps(-1) == 11 * kRev / 12);
}

// Every whole tick, both ways round the dial, and nothing in between: a cube on a shelf can
// only be turned a whole number of ticks and the hands may only ever move by whole ones.
void test_level_snaps_to_the_twelve_dots() {
    domain::Leveller lv;
    for (int t = 0; t < 12; ++t) {
        // The angle the cube is turned by, and the tick that answers it, are opposites: turn
        // the cube one dot clockwise and the dot at the top is one dot ANTICLOCKWISE.
        const float yaw = static_cast<float>(t) * 30.0f;
        CHECK(settle(lv, at(yaw)) == (12 - t) % 12);
    }
    // And a few degrees off a dot is still that dot -- nobody sets a cube down square.
    CHECK(settle(lv, at(0)) == 0);
    CHECK(settle(lv, at(97)) == 9);   // up at 263 deg -> the dot at 270
    CHECK(settle(lv, at(83)) == 9);   // ... and from the other side
    CHECK(settle(lv, at(-11)) == 0);  // 11 degrees of lean is not a new orientation
}

// The two rules that stop a reading being taken too seriously.  Both exist because the cost
// of a wrong answer is a hundred and eighty degrees of both hands, which is a second and a
// half of visible, audible, wrong movement.
void test_level_hysteresis_holds_a_tick_across_the_halfway_line() {
    domain::Leveller lv;
    CHECK(settle(lv, at(0)) == 0);
    // Exactly halfway between two dots.  Without hysteresis this is a coin toss taken again
    // on every poll; with it, the tick we are on wins.
    CHECK(settle(lv, at(15)) == 0);
    CHECK(settle(lv, at(-15)) == 0);
    // A degree past the halfway line is still not enough -- the band is six degrees wide.
    CHECK(settle(lv, at(18)) == 0);
    CHECK(settle(lv, at(-18)) == 0);
    // Far enough past it, and the dial does move.
    CHECK(settle(lv, at(24)) == 11);
    // ... and now the SAME angle that held tick 0 holds tick 11, which is what hysteresis
    // means: the answer depends on where you came from, and that is not a bug here.
    CHECK(settle(lv, at(15)) == 11);
}

void test_level_a_knock_is_not_an_orientation() {
    domain::Leveller lv;
    CHECK(settle(lv, at(0)) == 0);
    // One sample of a cube being lifted, straightened, or bumped into.  It agrees with
    // nothing before or after it, so it moves nothing.
    lv.update(at(90).x, at(90).y, at(90).z);
    CHECK(lv.tick() == 0);
    lv.update(at(0).x, at(0).y, at(0).z);
    CHECK(lv.tick() == 0);
    // Two in a row, and it is a shelf rather than an elbow.
    lv.update(at(90).x, at(90).y, at(90).z);
    CHECK(lv.tick() == 0);
    CHECK(lv.update(at(90).x, at(90).y, at(90).z));
    CHECK(lv.tick() == 9);
}

// The dial facing the ceiling: gravity is perpendicular to the glass and there is no answer
// to "which way up", because every way is.  Today the printed 12 wins (FlatPolicy::Zero);
// Hold is the other half of the argument and is one field away.
void test_level_flat_has_no_answer() {
    domain::Leveller lv;
    CHECK(settle(lv, at(90)) == 9);  // stood on its side first, so there is something to lose
    CHECK(settle(lv, at(90, 85)) == 0);
    CHECK(lv.level().flat);
    // Stand it back up and it finds the room again.
    CHECK(settle(lv, at(90, 0)) == 9);
    CHECK(!lv.level().flat);

    // The dead zone is a Schmitt: a clock propped at the threshold must not flicker.  Enter
    // it going down at ~72 degrees off vertical and leave it going up at ~66.
    domain::Leveller sc;
    CHECK(settle(sc, at(90, 0)) == 9);
    CHECK(settle(sc, at(90, 68)) == 9);  // tilt 0.37: past `flat_in`, not yet flat
    CHECK(settle(sc, at(90, 74)) == 0);  // tilt 0.28: flat now
    CHECK(settle(sc, at(90, 68)) == 0);  // ... and 0.37 is not enough to stand back up
    CHECK(settle(sc, at(90, 62)) == 9);  // 0.47 is
    CHECK(sc.level().tilt > 0.4f);

    // Holding is the same code with one field changed -- which is the whole reason the
    // policy is a field.
    domain::Leveller hold;
    hold.cfg.flat = domain::FlatPolicy::Hold;
    CHECK(settle(hold, at(90)) == 9);
    CHECK(settle(hold, at(90, 85)) == 9);
    CHECK(hold.level().flat);
}

// A sensor that is not answering, or answering rubbish, must move nothing at all.  This is
// the BNO085 that cannot be reset by firmware (R-BOARD-3) landing on the dial: degrade to
// "the printed 12 is the 12" and stay there, rather than to a dial that spins.
void test_level_rubbish_moves_nothing() {
    domain::Leveller lv;
    CHECK(settle(lv, at(90)) == 9);
    for (int i = 0; i < 10; ++i) lv.update(0.0f, 0.0f, 0.0f);
    CHECK(lv.tick() == 9);
    const float nan = std::nan("");
    for (int i = 0; i < 10; ++i) lv.update(nan, nan, nan);
    CHECK(lv.tick() == 9);
    // ... and a real reading afterwards is still believed.
    CHECK(settle(lv, at(0)) == 0);
}

// Turned slowly, a degree at a time, all the way round twice: every tick in order, no tick
// skipped, no tick visited twice in a row, and no reversal.  This is the property the whole
// file is really about -- a dial that follows the room monotonically.
void test_level_a_slow_turn_steps_once_per_dot() {
    domain::Leveller lv;
    settle(lv, at(0));
    int last = 0, changes = 0;
    bool ordered = true;
    for (int deg = 0; deg <= 720; ++deg) {
        const G g = at(static_cast<float>(deg));
        // Twice per degree: the poll is faster than a hand turning a cube, and the
        // confirmation count means a tick needs two agreeing samples to land.
        lv.update(g.x, g.y, g.z);
        if (lv.update(g.x, g.y, g.z)) {
            ++changes;
            // Anticlockwise round the dots, because the cube is going clockwise.
            ordered = ordered && (lv.tick() == (last + 11) % 12);
            last = lv.tick();
        }
    }
    CHECK(ordered);
    CHECK(changes == 24);
    CHECK(lv.tick() == 0);
}

void run_level_tests() {
    test_level_up_is_where_the_dial_says();
    test_level_snaps_to_the_twelve_dots();
    test_level_hysteresis_holds_a_tick_across_the_halfway_line();
    test_level_a_knock_is_not_an_orientation();
    test_level_flat_has_no_answer();
    test_level_rubbish_moves_nothing();
    test_level_a_slow_turn_steps_once_per_dot();
}
