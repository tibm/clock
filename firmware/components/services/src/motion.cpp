#include "clk/services/motion.hpp"

#include <algorithm>
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
constexpr int32_t kMinBackoff = kRev / 72;   // 5 deg -- the floor on the fine re-approach
constexpr int32_t kMaxBackoff = kRev / 6;    // 60 deg -- and its ceiling, under a big warp
constexpr int32_t kParkAway = kRev / 4;      // 90 deg: thirty times the width of the window
constexpr int32_t kClearSweep = kRev / 8;    // 45 deg is enough to prove a hand left the mark
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
        enter(Phase::Clear);
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
    dt_ms_ = dt;  // the fine re-approach sizes its back-off from this

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
        "", "clear", "coarse-minute", "fine-minute", "park-minute", "coarse-hour", "fine-hour", "",
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
        case Phase::Clear:
            // Deliberately moves nothing here.  `opto_` is last tick's reading, and a
            // `sim hand` (or a real hand nudged by a finger) landing between the two would
            // make this branch on a stale value.  The first tick decides, on fresh data.
            clear_stage_ = 0;
            clear_started_ = false;
            break;

        case Phase::CoarseMinute:
            CLK_LOGI(motion, "home: sweeping the minute hand for the index");
            hal::motor::run(Hand::Minute, t.v_coarse, pos(Hand::Minute) + kRev + kSweepMargin);
            break;
        case Phase::CoarseHour:
            CLK_LOGI(motion, "home: minute parked, sweeping the hour hand");
            hal::motor::run(Hand::Hour, t.v_coarse, pos(Hand::Hour) + kRev + kSweepMargin);
            break;

        case Phase::FineMinute:
        case Phase::FineHour: {
            // Back off into the dark and come at it again slowly.  An edge that repeats is
            // the difference between "we found the mark" and "we found a fingerprint".
            const Hand h = p == Phase::FineMinute ? Hand::Minute : Hand::Hour;
            fine_pass_ = 0;
            backoff_ = fine_backoff();
            hal::motor::run(h, -t.v_max / 4, pos(h) - backoff_);
            break;
        }

        case Phase::ParkMinute:
            // Exact and short: the minute hand's zero is already known, so this is a move
            // rather than a search.
            hal::motor::run(Hand::Minute, t.v_max, pos(Hand::Minute) + kParkAway);
            break;

        default:
            break;
    }
}

int32_t Motion::fine_backoff() const noexcept {
    const auto t = tuning();
    const int64_t travel = static_cast<int64_t>(t.v_coarse) * dt_ms_ * 3 / 1000;
    return static_cast<int32_t>(std::clamp<int64_t>(travel, kMinBackoff, kMaxBackoff));
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
        // Nothing downstream can trust a rising edge until the sensor is demonstrably dark:
        // a hand already parked on the index holds it lit forever and there is never an
        // edge to see.  With BOTH hands there it is also not knowable which one is doing
        // it, so this does not guess -- sweeping the hour hand a whole revolution without
        // going dark is itself the proof that the minute hand is the one on the mark.
        case Phase::Clear: {
            if (!high) {
                hal::motor::hold(Hand::Hour);
                hal::motor::hold(Hand::Minute);
                enter(Phase::CoarseMinute);
                return;
            }
            const Hand h = clear_stage_ == 0 ? Hand::Hour : Hand::Minute;
            if (!clear_started_) {
                clear_started_ = true;
                CLK_LOGI(motion, "home: sensor lit at rest -- moving the %s hand off it",
                         clear_stage_ == 0 ? "hour" : "minute");
                hal::motor::run(h, t.v_max, pos(h) + kClearSweep);
                phase_start_us_ = now;
                return;
            }
            if (hal::motor::state(h).moving) return;
            // kClearSweep is ~15x the width of the window and the sensor is read every tick
            // of it, so a hand that was on the mark has demonstrably left it and cannot have
            // come back round.  Still lit therefore means it was not that hand -- no
            // guessing, and no revolution spent proving it.
            if (clear_stage_ == 0) {
                clear_stage_ = 1;
                clear_started_ = false;
                return;
            }
            fail("sensor still reads bright with both hands moved clear -- check the QRE1113");
            return;
        }

        case Phase::CoarseMinute:
        case Phase::CoarseHour: {
            const bool is_minute = phase_ == Phase::CoarseMinute;
            const Hand h = is_minute ? Hand::Minute : Hand::Hour;
            if (rising) {
                hal::motor::hold(h);
                // Coarse: good to one sample of travel, which is all the fine pass needs in
                // order to know where to look.
                hal::motor::adopt(h, 0);
                enter(is_minute ? Phase::FineMinute : Phase::FineHour);
                return;
            }
            if (!hal::motor::state(h).moving) {
                fail(is_minute ? "the minute hand found no index in a full turn"
                               : "the hour hand found no index in a full turn");
            }
            return;
        }

        case Phase::FineMinute:
        case Phase::FineHour: {
            const bool is_minute = phase_ == Phase::FineMinute;
            const Hand h = is_minute ? Hand::Minute : Hand::Hour;
            if (fine_pass_ == 1 && rising) {
                hal::motor::hold(h);
                const int32_t err = domain::shortest(0, pos(h));
                if (std::abs(err) > backoff_) {
                    fail("the edge moved between passes");
                    return;
                }
                // The rising edge sits half an index-mark short of centre.  That systematic
                // offset is identical for both hands, so it shows up as one `motion zero`
                // trim on the bench rather than as an error between them.
                hal::motor::adopt(h, 0);
                CLK_LOGI(motion, "home: %s zero confirmed, coarse was off by %" PRId32 " usteps",
                         is_minute ? "minute" : "hour", err);
                if (is_minute) {
                    enter(Phase::ParkMinute);
                } else {
                    finish_home();
                }
                return;
            }
            if (hal::motor::state(h).moving) return;
            if (fine_pass_ == 0) {
                fine_pass_ = 1;
                opto_high_ = false;  // we backed off into the dark
                hal::motor::run(h, t.v_fine, pos(h) + 2 * backoff_);
                phase_start_us_ = now;
                return;
            }
            fail("the slow re-approach never saw the edge again");
            return;
        }

        // The minute hand's zero is known now, so the hour sweep gets a clear sensor
        // without anybody having to search for anything.
        case Phase::ParkMinute:
            if (!hal::motor::state(Hand::Minute).moving) enter(Phase::CoarseHour);
            return;

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
