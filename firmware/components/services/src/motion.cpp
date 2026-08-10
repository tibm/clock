#include "clk/services/motion.hpp"

#include <cinttypes>
#include <cmath>
#include <cstdlib>

#include "clk/domain/hand.hpp"
#include "clk/log.hpp"

namespace clk::svc {
namespace {

using domain::kRev;
using hal::motor::Hand;

constexpr uint32_t kTickMs = 10;             // 100 Hz control loop
constexpr uint32_t kPowerHoldMs = 2000;      // §6.1 hysteresis after the last move
constexpr uint32_t kHomeBudgetMs = 120'000;  // sim ms; a sweep that cannot find the index
constexpr uint32_t kPhaseBudgetMs = 45'000;
constexpr int32_t kVerifyBackoff = kRev / 90;     // 4 deg
constexpr int32_t kVerifyTolerance = kRev / 120;  // 3 deg -- generous; `motion zero` trims
constexpr int32_t kSweepMargin = kRev / 20;  // sweep a rev plus a bit, to cross a start-on-edge

const char* name_of(Motion::State s) noexcept {
    switch (s) {
        case Motion::State::Uninit:
            return "uninit";
        case Motion::State::Homing:
            return "homing";
        case Motion::State::Idle:
            return "idle";
        case Motion::State::Moving:
            return "moving";
        case Motion::State::Fault:
            return "fault";
    }
    return "?";
}

int32_t sgn(int32_t v) noexcept { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

}  // namespace

Motion::Motion() noexcept : ActiveObject({"motion", 20, 3584, kTickMs}) {}

Motion& motion() noexcept {
    static Motion m;
    return m;
}

// ---- public API (called from other threads) ----------------------------------------------

void Motion::goto_usteps(int32_t h, int32_t m, bool preview) noexcept {
    post(HandTarget{h, m, preview});
}

void Motion::nudge(Hand hand, int32_t usteps) noexcept {
    // `motion step` is a bench command and deliberately relative -- it is how you find
    // steps_per_rev before anything absolute means anything (§12.1 milestone 3).
    const int32_t h = hand == Hand::Hour ? pos(Hand::Hour) + usteps : pos(Hand::Hour);
    const int32_t m = hand == Hand::Minute ? pos(Hand::Minute) + usteps : pos(Hand::Minute);
    post(HandTarget{h, m, true});
}

void Motion::home() noexcept { post(HomeRequest{}); }
void Motion::halt() noexcept { post(Stop{}); }

Motion::Snapshot Motion::snapshot() const noexcept {
    port::Lock lk{mx_};
    return snap_;
}

Motion::Tuning Motion::tuning() const noexcept {
    port::Lock lk{mx_};
    return tune_;
}

void Motion::set_tuning(Tuning const& t) noexcept {
    port::Lock lk{mx_};
    tune_ = t;
}

// ---- lifecycle -----------------------------------------------------------------------------

void Motion::on_start() {
    CLK_LOGI(motion, "up; %" PRId32 " usteps/rev, not homed", kRev);
    state_ = State::Uninit;
    publish();
}

void Motion::on_event(Event const& e) {
    if (const auto* t = as<HandTarget>(e)) {
        want_h_ = t->hour_usteps;
        want_m_ = t->minute_usteps;
        want_valid_ = true;
        if (state_ == State::Homing || state_ == State::Fault) {
            CLK_LOGD(motion, "target held: %s", name_of(state_));
            return;
        }
        plan(hour_, t->hour_usteps);
        plan(min_, t->minute_usteps);
        if (hour_.active || min_.active) {
            power(true);
            state_ = State::Moving;
        }
        publish();
        return;
    }
    if (as<HomeRequest>(e)) {
        home_start_us_ = port::now_us();
        state_ = State::Homing;
        homed_ = false;
        power(true);
        enter(Phase::ParkMinute);
        publish();
        return;
    }
    if (as<Stop>(e)) {
        hal::motor::hold(Hand::Hour);
        hal::motor::hold(Hand::Minute);
        hour_.active = min_.active = false;
        if (state_ == State::Homing) state_ = homed_ ? State::Idle : State::Uninit;
        if (state_ == State::Moving) state_ = State::Idle;
        idle_since_us_ = port::now_us();
        publish();
    }
}

// ---- the control loop -------------------------------------------------------------------

void Motion::on_tick() {
    const auto o = hal::adc::read_opto_norm();
    opto_ = o.ok() ? o.v : 0.0f;

    // Measured, not nominal.  The AO's poll bound is REAL milliseconds, so under a large
    // `sim warp` a tick can represent far more than kTickMs of sim time -- and a ramp
    // computed from the nominal number would then accelerate fifty times too slowly.
    // (Warp far enough and the opto is sampled too rarely to catch the index at all.  That
    // is not a bug to paper over here: it is the same aliasing a too-fast sweep hits on the
    // bench, and the fake models it on purpose.)
    const uint64_t now = port::now_us();
    const uint32_t dt_ms =
        last_tick_us_ == 0 ? kTickMs : static_cast<uint32_t>((now - last_tick_us_) / 1000ull);
    last_tick_us_ = now;
    const uint32_t dt = dt_ms < 1 ? 1 : (dt_ms > 200 ? 200 : dt_ms);

    if (state_ == State::Homing) {
        run_homing(opto_);
    } else if (state_ == State::Moving) {
        const bool a = step_axis(hour_, dt);
        const bool b = step_axis(min_, dt);
        if (!a && !b) {
            state_ = State::Idle;
            idle_since_us_ = port::now_us();
            if (sub_) sub_->post(HandState{pos(Hand::Hour), pos(Hand::Minute), false, homed_});
            CLK_LOGD(motion, "settled h=%" PRId32 " m=%" PRId32, pos(Hand::Hour),
                     pos(Hand::Minute));
        }
    }

    // Coils de-energised 2 s after the last move.  The hands are stationary >99 % of the
    // time (§6.1) and holding current the whole while would be most of the power budget.
    if (powered_ && state_ != State::Moving && state_ != State::Homing &&
        port::now_us() - idle_since_us_ > kPowerHoldMs * 1000ull) {
        power(false);
    }
    publish();
}

// ---- trajectory ------------------------------------------------------------------------

void Motion::plan(Axis& ax, int32_t target) noexcept {
    Tuning t;
    {
        port::Lock lk{mx_};
        t = tune_;
    }
    const int32_t from = pos(ax.hand);
    const auto ap = domain::approach(from, target, t.backlash);
    ax.via = ap.via;
    ax.target = ap.target;
    ax.leg2 = (ap.via == ap.target);
    ax.active = (ap.via != from || ap.target != from);
    ax.v = 0;
}

// One tick of a trapezoidal profile.  The deceleration limit is the only interesting line:
// v <= sqrt(2*a*s) is what guarantees we can still stop in the distance that is left, which
// is what makes the landing exact rather than a series of corrections.
bool Motion::step_axis(Axis& ax, uint32_t dt_ms) noexcept {
    if (!ax.active) return false;
    Tuning t;
    {
        port::Lock lk{mx_};
        t = tune_;
    }

    const int32_t p = pos(ax.hand);
    int32_t leg = ax.leg2 ? ax.target : ax.via;
    int32_t remaining = leg - p;

    if (remaining == 0) {
        if (!ax.leg2) {
            // Undershot for backlash; now come back up through the slop so the move ends
            // clockwise, which is the whole point (§6.1).
            ax.leg2 = true;
            ax.v = 0;
            leg = ax.target;
            remaining = leg - p;
            if (remaining == 0) {
                ax.active = false;
                hal::motor::hold(ax.hand);
                return false;
            }
        } else {
            ax.active = false;
            hal::motor::hold(ax.hand);
            return false;
        }
    }

    const double dt = dt_ms / 1000.0;
    const int32_t dist = std::abs(remaining);
    const auto v_stop = static_cast<int32_t>(std::sqrt(2.0 * t.accel * dist));
    int32_t v = ax.v + static_cast<int32_t>(t.accel * dt);
    if (v > t.v_max) v = t.v_max;
    if (v > v_stop) v = v_stop;
    if (v < 1) v = 1;
    ax.v = v;

    hal::motor::run(ax.hand, sgn(remaining) * v, leg);
    return true;
}

void Motion::power(bool on) noexcept {
    // Ask the driver rather than trusting a cached belief.  A cache that says "the coils are
    // live" while STEP_STBY is actually low is a clock whose hands quietly stop -- and the
    // ways that can happen (brownout, an expander reset, a reconnected harness) are all
    // things that do not announce themselves.
    powered_ = hal::motor::enabled();
    if (on == powered_) return;
    // On target this is a MotorPower event to `board`, which clears STEP_STBY over I2C and
    // replies (§6.1).  The HAL keeps the same shape so this line does not change.
    const Status st = hal::motor::enable(on);
    if (st != Status::Ok) {
        CLK_LOGW(motion, "motor power %s: %s", on ? "on" : "off", clk::name(st));
        return;
    }
    powered_ = on;
    if (!on) {
        hour_.active = min_.active = false;
    }
    CLK_LOGD(motion, "coils %s", on ? "live" : "off");
}

// Snapshot for `motion status` and the UI bridge, both of which read from other threads.
void Motion::publish() noexcept {
    static constexpr const char* kPhaseNames[] = {
        "", "park-minute", "sweep-hour", "park-hour", "sweep-minute", "verify", "",
    };
    Snapshot s;
    s.state = state_;
    s.state_name = name_of(state_);
    s.phase = kPhaseNames[static_cast<std::size_t>(phase_)];
    s.hour = pos(Hand::Hour);
    s.minute = pos(Hand::Minute);
    s.target_hour = hour_.active ? hour_.target : s.hour;
    s.target_minute = min_.active ? min_.target : s.minute;
    s.homed = homed_;
    s.powered = powered_;
    s.opto = opto_;
    s.faults = faults_;
    port::Lock lk{mx_};
    const uint32_t keep = snap_.home_ms;
    s.home_ms = keep;
    snap_ = s;
}

// ---- homing ------------------------------------------------------------------------------

void Motion::enter(Phase p) noexcept {
    const Tuning t = tuning();
    phase_ = p;
    phase_start_us_ = port::now_us();
    // Latch the CURRENT level, so a sweep that starts with the sensor already lit waits for
    // it to go dark and rise again instead of "finding" the mark it was parked on.
    opto_high_ = opto_ > t.opto_thresh;

    switch (p) {
        case Phase::ParkMinute:
            CLK_LOGI(motion, "home: sweeping the minute hand to find the index");
            edge_pos_ = INT32_MIN;  // sentinel: no edge seen in this phase yet
            hal::motor::run(Hand::Minute, t.v_home, pos(Hand::Minute) + kRev + kSweepMargin);
            break;
        case Phase::SweepHour:
            CLK_LOGI(motion, "home: minute parked, sweeping the hour hand");
            hal::motor::run(Hand::Hour, t.v_home, pos(Hand::Hour) + kRev + kSweepMargin);
            break;
        case Phase::ParkHour:
            hal::motor::run(Hand::Hour, t.v_max / 2, pos(Hand::Hour) + kRev / 2);
            break;
        case Phase::SweepMinute:
            CLK_LOGI(motion, "home: hour parked, sweeping the minute hand");
            hal::motor::run(Hand::Minute, t.v_home, pos(Hand::Minute) + kRev + kSweepMargin);
            break;
        case Phase::Verify:
            // Back off and come at it again at quarter speed.  A repeatable edge is the
            // difference between "we found the mark" and "we found a fingerprint".
            verify_pass_ = 0;
            hal::motor::run(Hand::Minute, -t.v_max / 4, pos(Hand::Minute) - kVerifyBackoff);
            break;
        default:
            break;
    }
}

void Motion::fail(const char* why) noexcept {
    CLK_LOGE(motion, "home failed: %s", why);
    ++faults_;
    hal::motor::hold(Hand::Hour);
    hal::motor::hold(Hand::Minute);
    state_ = State::Fault;
    phase_ = Phase::None;
    power(false);
    if (sub_) sub_->post(HomeDone{false, 0});
}

void Motion::run_homing(float opto) noexcept {
    Tuning t = tuning();
    const uint64_t now = port::now_us();
    if (now - home_start_us_ > kHomeBudgetMs * 1000ull) return fail("budget exhausted");
    if (now - phase_start_us_ > kPhaseBudgetMs * 1000ull) return fail("phase stalled");

    const bool high = opto > t.opto_thresh;
    const bool rising = high && !opto_high_;
    opto_high_ = high;

    switch (phase_) {
        case Phase::ParkMinute: {
            // The index is wherever the minute hand first lights the sensor.  We do not
            // adopt here -- this pass only exists to get the minute hand demonstrably OFF
            // the mark so the hour sweep sees the hour hand and nothing else.
            if (rising) {
                hal::motor::hold(Hand::Minute);
                hal::motor::run(Hand::Minute, t.v_max / 2, pos(Hand::Minute) + kRev / 2);
                phase_start_us_ = now;
                edge_pos_ = pos(Hand::Minute);
                CLK_LOGD(motion, "home: minute found the index, parking half a turn on");
                return;
            }
            if (!hal::motor::state(Hand::Minute).moving) {
                if (edge_pos_ != INT32_MIN) {
                    enter(Phase::SweepHour);
                } else {
                    // A full revolution with no edge: either the hour hand is sitting on
                    // the mark and holding the sensor bright, or there is no mark at all.
                    fail(high ? "sensor stuck bright -- is the hour hand on the index?"
                              : "no index found in one revolution");
                }
            }
            return;
        }

        case Phase::SweepHour: {
            if (rising) {
                hal::motor::hold(Hand::Hour);
                // The rising edge is half an index-mark short of centre.  That systematic
                // offset is identical for both hands, so it shows up as one `motion zero`
                // trim on the bench rather than as an error between them.
                hal::motor::adopt(Hand::Hour, 0);
                CLK_LOGI(motion, "home: hour edge -> 0");
                enter(Phase::ParkHour);
                return;
            }
            if (!hal::motor::state(Hand::Hour).moving) fail("hour hand found no index");
            return;
        }

        case Phase::ParkHour:
            if (!hal::motor::state(Hand::Hour).moving) enter(Phase::SweepMinute);
            return;

        case Phase::SweepMinute: {
            if (rising) {
                hal::motor::hold(Hand::Minute);
                hal::motor::adopt(Hand::Minute, 0);
                CLK_LOGI(motion, "home: minute edge -> 0");
                enter(Phase::Verify);
                return;
            }
            if (!hal::motor::state(Hand::Minute).moving) fail("minute hand found no index");
            return;
        }

        case Phase::Verify: {
            if (verify_pass_ == 1 && rising) {
                hal::motor::hold(Hand::Minute);
                const int32_t err = domain::shortest(0, pos(Hand::Minute));
                if (std::abs(err) > kVerifyTolerance) {
                    fail("the edge moved between passes");
                    return;
                }
                CLK_LOGI(motion, "home: verified, edge repeats within %" PRId32 " usteps", err);
                hal::motor::adopt(Hand::Minute, 0);
                finish_home();
                return;
            }
            if (hal::motor::state(Hand::Minute).moving) return;
            if (verify_pass_ == 0) {
                verify_pass_ = 1;
                opto_high_ = false;
                hal::motor::run(Hand::Minute, t.v_verify, pos(Hand::Minute) + 2 * kVerifyBackoff);
                phase_start_us_ = now;
                return;
            }
            fail("the re-approach never saw the edge again");
            return;
        }

        default:
            return;
    }
}

void Motion::finish_home() noexcept {
    homed_ = true;
    phase_ = Phase::Done;
    const auto took = static_cast<uint32_t>((port::now_us() - home_start_us_) / 1000u);
    CLK_LOGI(motion, "homed in %" PRIu32 " ms of sim time", took);
    {
        port::Lock lk{mx_};
        snap_.home_ms = took;
    }
    state_ = State::Idle;
    idle_since_us_ = port::now_us();
    if (sub_) sub_->post(HomeDone{true, took});
    // Whatever the clock wanted while we were busy is still what it wants.
    if (want_valid_) post(HandTarget{want_h_, want_m_, false});
}

}  // namespace clk::svc
