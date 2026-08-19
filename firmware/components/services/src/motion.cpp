#include "clk/services/motion.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdlib>

#include "clk/board.hpp"
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

// ---- auto-home (§6.1): a crossing of the index is a free calibration --------------------
//
// The numbers that decide whether a crossing is worth believing:
constexpr int32_t kTrimWindow = kRev / 240;      // 1.5 deg -- further off than this is not drift
constexpr int32_t kTrimApart = kRev / 72;        // 5 deg -- how far the OTHER hand must be, or
                                                 //   nothing can say which of the two lit it
constexpr int32_t kTrimPrecision = kRev / 1440;  // 0.25 deg of travel per ADC sample, tops
constexpr uint8_t kLostBeforeHome = 3;           // ... in a row, and the movement has slipped

// NVS keys (§7.5).  Fifteen characters is the NVS limit and these are thirteen.
constexpr const char* kKeyZeroH = "motion.zero_h";
constexpr const char* kKeyZeroM = "motion.zero_m";

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

void Motion::goto_usteps(int32_t h, int32_t m, bool preview, int dir) noexcept {
    post(HandTarget{h, m, preview, static_cast<int8_t>(dir > 0 ? 1 : (dir < 0 ? -1 : 0))});
}

void Motion::nudge(Hand hand, int32_t usteps) noexcept {
    // `motion step` is a bench command and deliberately relative -- it is how you find
    // steps_per_rev before anything absolute means anything (§12.1 milestone 3).
    const int32_t h = hand == Hand::Hour ? pos(Hand::Hour) + usteps : pos(Hand::Hour);
    const int32_t m = hand == Hand::Minute ? pos(Hand::Minute) + usteps : pos(Hand::Minute);
    post(HandTarget{h, m, true});
}

void Motion::home() noexcept { post(HomeRequest{}); }
// Halt, not Stop: `Stop` is the AO framework's shutdown event and never reaches on_event().
void Motion::halt() noexcept { post(Halt{}); }

void Motion::set_zero(Hand h, int32_t usteps) noexcept {
    post(ZeroSet{static_cast<uint8_t>(h), usteps});
}

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
    // The zeros are not a setting to be assigned: changing one moves a hand and shifts every
    // pending target with it, so it goes through set_zero() and this keeps what it had.
    const int32_t zh = tune_.zero_h, zm = tune_.zero_m;
    tune_ = t;
    tune_.zero_h = zh;
    tune_.zero_m = zm;
}

// ---- lifecycle -----------------------------------------------------------------------------

void Motion::on_start() {
    // The calibration first: it is what the homing run below is going to adopt.  A key that
    // has never been written answers NotPresent and leaves the compiled-in zero, which is the
    // right answer for a movement nobody has trimmed yet.
    {
        port::Lock lk{mx_};
        const auto zh = hal::store::get_i32(kKeyZeroH);
        const auto zm = hal::store::get_i32(kKeyZeroM);
        if (zh.ok()) tune_.zero_h = zh.v;
        if (zm.ok()) tune_.zero_m = zm.v;
        if (zh.ok() || zm.ok()) {
            CLK_LOGI(motion, "zero h=%" PRId32 " m=%" PRId32 " usteps (stored)", tune_.zero_h,
                     tune_.zero_m);
        }
    }
    CLK_LOGI(motion, "up; %" PRId32 " usteps/rev, not homed", kRev);
    state_ = State::Uninit;
    publish();

    // And then home, unasked.  The hands are wherever the last power-off left them, nothing
    // else can find that out, and every reading the clock gives until it does is a guess --
    // so the first thing a booted movement does is go and look (§6.1).  Absent hardware is
    // not a failure to report: on a devkit with nothing wired this is simply not the day.
    if (!home_on_start_) {
        CLK_LOGI(motion, "boot homing disabled -- `motion home` when you want it");
    } else if (board::present(board::Dev::Motor) && board::present(board::Dev::Opto)) {
        CLK_LOGI(motion, "homing on boot");
        home();
    } else {
        CLK_LOGI(motion, "no movement fitted -- not homing");
    }
}

void Motion::on_event(Event const& e) {
    if (const auto* t = as<HandTarget>(e)) {
        want_h_ = resolve(want_h_, pos(Hand::Hour), t->hour_usteps, t->dir);
        want_m_ = resolve(want_m_, pos(Hand::Minute), t->minute_usteps, t->dir);
        want_valid_ = true;
        if (state_ == State::Homing || state_ == State::Fault) {
            CLK_LOGD(motion, "target held: %s", name_of(state_));
            return;
        }
        retarget();
        publish();
        return;
    }
    if (const auto* z = as<ZeroSet>(e)) {
        const Hand h = static_cast<Hand>(z->hand);
        int32_t d = 0;
        {
            port::Lock lk{mx_};
            int32_t& zero = h == Hand::Hour ? tune_.zero_h : tune_.zero_m;
            d = z->usteps - zero;
            zero = z->usteps;
        }
        hal::store::set_i32(h == Hand::Hour ? kKeyZeroH : kKeyZeroM, z->usteps);
        CLK_LOGI(motion, "zero %s = %" PRId32 " usteps (%+.2f deg)",
                 h == Hand::Hour ? "hour" : "minute", z->usteps,
                 static_cast<double>(z->usteps) * 360.0 / kRev);
        // A movement that has never found its index has no frame to shift: the number is
        // stored and the next home adopts it.  One that HAS is trimmed live, hand and all,
        // because a calibration you cannot watch land is a calibration nobody can perform.
        if (d != 0 && homed_ && state_ != State::Homing) {
            shift_frame(h, -d);
            retarget();
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
    if (as<Halt>(e)) {
        hal::motor::hold(Hand::Hour);
        hal::motor::hold(Hand::Minute);
        hour_.active = min_.active = false;
        // An abort is not a fault: nothing failed, we were asked to stop.  But a half-finished
        // homing run has not established a zero, so say so rather than keeping a stale one.
        if (state_ == State::Homing) {
            state_ = homed_ ? State::Idle : State::Uninit;
            phase_ = Phase::None;
        }
        if (state_ == State::Moving) state_ = State::Idle;
        // Whatever chrono last asked for is no longer being driven towards, so a later
        // `follow` has to push it again rather than assume it is still on its way.
        want_valid_ = false;
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
    } else {
        // Auto-home.  Homing is what finds the zero; this is what keeps it, and it costs one
        // comparison per tick because the sensor is already being read.
        watch_index(opto_);
    }

    if (state_ == State::Moving) {
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

// Where a target IS, in the hands' own unwrapped frame.
//
// Callers speak in dial positions, 0..kRev, and a dial position is ambiguous by a whole number
// of turns -- which matters here, because the hands are counted in unwrapped microsteps and
// can be several turns from zero.  So EVERY target is resolved to that frame on arrival:
//
//   dir == 0  the shortest way there from where the hand is.  What a clock means.
//   dir != 0  a STEP of whatever the knob is turning, accumulated onto the last setpoint
//             rather than measured from the hand.  The hand can be most of a turn behind a
//             knob being wound, and asking a lagging hand to go "anticlockwise to 11:55" the
//             moment the user backs off a minute would send it 350 degrees the wrong way to a
//             place it is 10 degrees short of.  Accumulating says what the user actually did
//             -- the setting went back one minute -- and lets the hand close the gap the way
//             it was already going.
//
// Both come out as an absolute unwrapped position, so plan() can simply chase it.
int32_t Motion::resolve(int32_t prev, int32_t from, int32_t to, int dir) const noexcept {
    if (dir == 0) return from + domain::shortest(from, to);
    const int32_t base = want_valid_ ? prev : from;
    return base + domain::directed(base, to, dir);
}

// Both axes onto what was last asked for, and go if that turned out to be somewhere else.
void Motion::retarget() noexcept {
    plan(hour_, want_h_);
    plan(min_, want_m_);
    if (hour_.active || min_.active) {
        power(true);
        state_ = State::Moving;
    }
}

Motion::Axis& Motion::axis_of(Hand h) noexcept { return h == Hand::Hour ? hour_ : min_; }

// Where the index sits in a hand's own frame.  Homing adopts `-zero` when it finds the edge,
// so that is where the edge is by definition -- and moving the zero moves this with it, which
// is what makes the manual trim and the automatic one talk about the same place (§6.1).
int32_t Motion::index_pos(Hand h) const noexcept {
    const auto t = tuning();
    return domain::normalise(-(h == Hand::Hour ? t.zero_h : t.zero_m));
}

// Rename where the hand IS by `d`, and leave every target exactly where it was.
//
// That asymmetry is the whole mechanism, and it is worth being explicit about because the
// obvious "shift the targets too" is wrong in both directions.  A target is a LABEL in this
// frame -- "12:05" is 1440 -- and the reason we are shifting is that the frame was wrong: the
// label pointed a few microsteps off north, and after the shift the same label points at the
// right place.  Moving the labels as well would carry the error forward untouched and no hand
// would move at all.  So: adopt() renames the position without turning the shaft, step_axis()
// re-issues the unchanged target on the next tick, and the hand travels the difference.
void Motion::shift_frame(Hand h, int32_t d) noexcept {
    if (d == 0) return;
    hal::motor::adopt(h, pos(h) + d);
}

void Motion::plan(Axis& ax, int32_t target) noexcept {
    Tuning t;
    {
        port::Lock lk{mx_};
        t = tune_;
    }
    const int32_t from = pos(ax.hand);
    // Which way we were already going, before the new target overwrites the old legs.
    const int32_t was_dir = ax.active ? sgn((ax.leg2 ? ax.target : ax.via) - from) : 0;

    // resolve() has already put the target in this frame, so: the way it lies, whole
    // revolutions dropped.  For a shortest-way target that is the shortest way, unchanged.
    const auto ap = domain::approach_by(from, domain::chase(from, target), t.backlash);
    ax.via = ap.via;
    ax.target = ap.target;
    ax.leg2 = (ap.via == ap.target);
    ax.active = (ap.via != from || ap.target != from);

    // Re-planning must not throw the ramp away.  chrono re-targets every time its computed
    // position moves -- about five times a second for the minute hand, and EVERY target
    // re-plans both axes -- so zeroing here made a cruising hand drop to a standstill and
    // ramp up again several times per move.  On the hour hand, whose own moves are short,
    // that was the whole move.  Keep the speed when the new leg runs the same way as the
    // old one; step_axis() still clamps it to sqrt(2*a*s), so the landing stays exact.
    const int32_t now_dir = sgn((ax.leg2 ? ax.target : ax.via) - from);
    if (was_dir == 0 || now_dir != was_dir) ax.v = 0;
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
    s.trims = trims_;
    s.last_trim = last_trim_;
    port::Lock lk{mx_};
    const uint32_t keep = snap_.home_ms;
    s.home_ms = keep;
    s.zero_h = tune_.zero_h;
    s.zero_m = tune_.zero_m;
    snap_ = s;
}

// ---- auto-home: the index, crossed in the ordinary course of telling the time -------------
//
// Homing establishes the zero once, in nine seconds of sweeping nobody wants to watch twice.
// But the hands cross that same index every hour of every day, and each crossing measures the
// same thing for free -- so a movement that is drifting (a missed microstep, a knocked cube,
// a shaft that slipped in the gear train) can put itself right without ever saying so.
//
// Four rules keep it from making things worse, and every one of them exists because a wrong
// correction is worse than no correction:
//
//  1. CLOCKWISE only.  The rising edge is the one homing adopted on, and it sits on the far
//     side of the window when approached the other way -- half a window of systematic error
//     if we did not care which direction the hand came from.
//  2. SLOW only.  The sensor is sampled once a tick, so a crossing is known to within one
//     sample of travel.  At a slew that is a degree and a half, which is the whole accept
//     window; while the clock is simply telling the time it is a few microsteps.  The hands
//     spend >99 % of their life in the second case, so requiring it costs nothing and makes
//     the measurement exact.  (What is left is halved out: the edge happened somewhere in the
//     last sample, so the best estimate of where the hand was is half a sample back.)
//  3. ONE candidate.  Both hands pass the same window and at 12:00 they are both sitting in
//     it.  If the other hand is anywhere near, nothing can say which one lit the sensor, and
//     a guess would be a coin toss that moves a hand.
//  4. Within the window, or it is not drift.  Three crossings in a row that land nowhere near
//     where they should mean the movement has genuinely slipped -- and the answer to that is
//     not a bigger trim, it is a real home.
void Motion::watch_index(float opto) noexcept {
    const Tuning t = tuning();
    const bool high = opto > t.opto_thresh;
    const bool rising = high && !opto_high_;
    opto_high_ = high;
    if (!rising || !homed_ || !t.autohome || state_ == State::Fault) return;

    struct Look {
        int32_t err;  // where the index is, from where the hand thinks it is
        bool near;    // ... close enough that this hand COULD be what lit the sensor
        bool usable;  // clockwise, and slow enough for the reading to mean anything
    } look[2]{};

    const Hand kHands[2] = {Hand::Hour, Hand::Minute};
    for (int i = 0; i < 2; ++i) {
        const auto ax = hal::motor::state(kHands[i]);
        // Where the hand was when the edge actually happened, not where the sample caught it.
        const int32_t travel = static_cast<int32_t>(static_cast<int64_t>(ax.vel) * dt_ms_ / 1000);
        const int32_t at = pos(kHands[i]) - travel / 2;
        look[i].err = domain::shortest(at, index_pos(kHands[i]));
        look[i].near = std::abs(look[i].err) <= kTrimApart;
        look[i].usable = ax.vel > 0 && travel <= kTrimPrecision;
    }

    // Rule 2 first, and it gates the whole mechanism rather than just the correction: if
    // nothing was crossing slowly enough for the sample to mean anything, this edge is not a
    // measurement and NO conclusion may be drawn from it -- not "the hand is out by half a
    // degree", and not "the hands are lost" either.  A slew at 48 000 usteps/s covers ten
    // degrees between two reads, which is further than the attribution window is wide; before
    // this returned, a long fast wind looked exactly like a movement that had slipped and
    // re-homed itself in the middle of one.
    if (!look[0].usable && !look[1].usable) return;

    if (look[0].near && look[1].near) {
        CLK_LOGD(motion, "index: both hands are on it -- nothing can say which");
        return;
    }
    if (!look[0].near && !look[1].near) {
        // A hand WAS crossing, slowly and clockwise, and neither hand believes it is anywhere
        // near the index.  That is a movement that has lost its place, and rule 4 counts it.
        if (++lost_ >= kLostBeforeHome) {
            CLK_LOGW(motion, "index lit with neither hand near it, %u times -- re-homing", lost_);
            lost_ = 0;
            home();
        }
        return;
    }

    const int i = look[0].near ? 0 : 1;
    const Hand best = kHands[i];
    const int32_t best_err = look[i].err;
    if (!look[i].usable) return;  // the hand at the index is not the one that measured
    if (std::abs(best_err) > kTrimWindow) {
        if (++lost_ < kLostBeforeHome) {
            CLK_LOGW(motion, "index crossed %" PRId32 " usteps out (%u/%u) -- watching", best_err,
                     lost_, kLostBeforeHome);
            return;
        }
        CLK_LOGW(motion, "index %u crossings out of place -- the hands have slipped, re-homing",
                 lost_);
        lost_ = 0;
        home();
        return;
    }
    lost_ = 0;
    if (best_err == 0) return;
    trim_hand(best, best_err);
}

void Motion::trim_hand(Hand h, int32_t err) noexcept {
    // adopt() implies hold(), so a hand that was moving stops for exactly one tick: step_axis
    // re-issues run() below with the shifted leg and the speed it already had, and the hand
    // carries on.  Ten milliseconds, once an hour, a few microsteps -- nobody sees it.
    shift_frame(h, err);
    ++trims_;
    last_trim_ = err;
    CLK_LOGI(motion, "auto-home: %s hand was %" PRId32 " usteps (%+.2f deg) out -- corrected",
             h == Hand::Hour ? "hour" : "minute", err, static_cast<double>(err) * 360.0 / kRev);
    if (axis_of(h).active && state_ != State::Moving) state_ = State::Moving;
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
                // order to know where to look.  `index_pos` rather than 0 -- the per-unit
                // trim is where the two frames meet, and both passes adopt in the same one.
                hal::motor::adopt(h, index_pos(h));
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
                const int32_t err = domain::shortest(index_pos(h), pos(h));
                if (std::abs(err) > backoff_) {
                    fail("the edge moved between passes");
                    return;
                }
                // The rising edge sits half an index-mark short of centre.  That systematic
                // offset is identical for both hands, so it comes out as one `motion zero`
                // trim per hand on the bench rather than as an error between them -- which is
                // exactly what index_pos() is: the edge is the zero, plus what you measured.
                hal::motor::adopt(h, index_pos(h));
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
    lost_ = 0;  // whatever the hands had lost, they have just found again
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
