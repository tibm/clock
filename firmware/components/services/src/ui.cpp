#include "clk/services/ui.hpp"

#include <cinttypes>
#include <cstdlib>

#include "clk/domain/hand.hpp"
#include "clk/log.hpp"

namespace clk::svc {
namespace {

using hal::pixels::Rgbw;

constexpr uint32_t kTickMs = 20;          // §6.6: PCNT is polled and diffed every 20 ms
constexpr uint8_t kPowerEveryTicks = 12;  // ~4 Hz; it is a battery, not a trigger
constexpr uint8_t kTapEveryTicks = 2;     // 10 Hz: a snooze tap must not feel laggy
constexpr uint8_t kLowBattPct = 20;

// How often gravity is asked which way up the cube is (§6.1d).  The two numbers ARE the
// feature's power budget: plugged in you can turn the cube on the shelf and watch the hands
// come round after it, so the poll has to be fast enough to feel like a response rather than
// a refresh; on the battery nobody is watching a clock they are carrying, and the same answer
// costs four times less.  Sim time, so `sim warp` scales them with everything else.
constexpr uint32_t kLevelPluggedMs = 500;
constexpr uint32_t kLevelBatteryMs = 2000;
constexpr uint32_t kTapAckMs = 400;  // the tap flash, long enough to be seen on a 50 Hz feed

// Chain order is dial first (§9.2): 0-1 on-PCB dial wash, 2-6 the status row through J12.
constexpr std::size_t kBell = 2, kAlarmPx = 3, kClockPx = 4, kVol = 5, kBatt = 6;

// The volume gauge (README §12): 0 % straight up, 100 % at 10 o'clock the long way round,
// so the whole range is 300 degrees of dial and both hands carry it together.  The remaining
// 60 degrees -- between the 10 and the 12 -- is off the scale, and the hands never go there:
// every move inside the mode carries the direction the level is changing, so the gauge is
// swept rather than short-cut across its own dead zone.
constexpr int32_t kVolSweepDeg = 300;
constexpr int32_t kUstepsPerVolPct = domain::kRev * kVolSweepDeg / 360 / 100;
static_assert(kUstepsPerVolPct * 100 == domain::kRev * kVolSweepDeg / 360, "gauge must be exact");

// Both hands stacked on the 6.  Deliberately not a time: at 6:30 the hour hand is halfway to
// the 7, so a pair of hands agreeing on the 6 is a reading no working clock ever shows -- and
// that is the point, it is what "the alarm is off" looks like (§6.6b).
constexpr int32_t kSouth = domain::kRev / 2;

// Setting a time is PACED to what the movement can draw (§6.6d, drain_setting below).  The
// pace itself is derived from `motion`'s v_max; these are the bounds on it.
constexpr uint32_t kPaceFloorMs = 20;  // never faster than `ui`'s own tick
constexpr uint32_t kPaceCeilMs = 500;  // ... and never so slow the knob feels dead
// The encoder has no detent -- it is optical, 256 counts/rev -- but four counts is the notch a
// detented knob would have, and it is the unit `ui knob counts` is quoted in.  drain_setting()
// will not split one.
constexpr int32_t kDetentCounts = 4;

// The preview chime, until `audio` (§6.2) owns the amp: you cannot set a volume you cannot
// hear, so the mode plays at the level it is editing.
constexpr uint32_t kChimeMs = 140;
constexpr uint32_t kChimeEveryMs = 1500;

const char* name_of(Ui::Mode m) noexcept {
    switch (m) {
        case Ui::Mode::Idle:
            return "idle";
        case Ui::Mode::Bell:
            return "bell";
        case Ui::Mode::Alarm:
            return "alarm";
        case Ui::Mode::Clock:
            return "clock";
        case Ui::Mode::Volume:
            return "volume";
        case Ui::Mode::Pairing:
            return "pairing";
    }
    return "?";
}

int wrap_day(int minutes) noexcept {
    minutes %= 1440;
    return minutes < 0 ? minutes + 1440 : minutes;
}

}  // namespace

Ui::Ui() noexcept : ActiveObject({"ui", 10, 4096, kTickMs}) {}

Ui& ui() noexcept {
    static Ui u;
    return u;
}

void Ui::on_start() {
    const auto k = hal::knob::read();
    knob_last_ = k.ok() ? k.v.count : 0;
    // Seed the tap counter here, exactly as the knob is seeded: whatever it already reads is
    // history, not a tap the user just made.
    const auto s = hal::imu::read();
    taps_last_ = s.ok() ? s.v.taps : 0;
    last_input_us_ = port::now_us();
    CLK_LOGI(ui, "up; knob %s", k.ok() ? "present" : clk::name(k.st));
    cue();
    render();
    publish();
}

Ui::Snapshot Ui::snapshot() const noexcept {
    port::Lock lk{mx_};
    return snap_;
}
Ui::Tuning Ui::tuning() const noexcept {
    port::Lock lk{mx_};
    return tune_;
}
void Ui::set_tuning(Tuning const& t) noexcept {
    port::Lock lk{mx_};
    tune_ = t;
}
domain::AnimCfg Ui::anim_cfg() const noexcept {
    port::Lock lk{mx_};
    return anim_;
}
void Ui::set_anim_cfg(domain::AnimCfg const& c) noexcept {
    port::Lock lk{mx_};
    anim_ = c;
}

void Ui::set_mode(Mode m) noexcept { post(ModeSet{static_cast<uint8_t>(m)}); }

void Ui::on_event(Event const& e) {
    if (const auto* m = as<ModeSet>(e)) return enter(static_cast<Mode>(m->mode));
    if (const auto* d = as<KnobDelta>(e)) return rotate(d->counts);
    if (const auto* p = as<KnobPress>(e)) {
        if (!p->down) press(p->held_ms);
        return;
    }
    if (as<Tap>(e)) {
        // Tap-to-snooze (README §12).  Until the alarm exists it is a visible
        // acknowledgement, which is still the interaction worth tuning: a tap must feel
        // like it did something.  It goes on the OVERLAY layer, so it outranks whatever the
        // mode is showing and hands the pixel straight back when it has faded -- the
        // previous arrangement painted over it on the next tick and nobody ever saw it.
        CLK_LOGI(ui, "tap");
        arm(over_, kBell, domain::ramp_down(domain::kWhite, level(), kTapAckMs));
        last_input_us_ = port::now_us();
    }
}

void Ui::on_tick() {
    poll_knob();
    poll_tap();
    poll_level();
    watch_battery();
    drain_setting();  // release banked counts at the speed the hands can render them

    // ONE timeout, and every mode obeys it -- pairing included.  A second number for a second
    // mode is a second thing to discover, and the knob's whole contract is that whatever you
    // last touched goes away five seconds after you stop touching it.
    const auto t = tuning();
    if (mode_ != Mode::Idle && port::now_us() - last_input_us_ > t.timeout_ms * 1000ull) {
        CLK_LOGI(ui, "timeout -> idle (settings kept)");
        enter(Mode::Idle);
    }

    chime_tick();
    cue();
    render();
    publish();
}

// Nothing posted Tap.  The event existed, the handler below existed, `sim tap` and the app's
// tap button existed -- and in between there was no code at all, so the one gesture README
// §12 calls tap-to-snooze incremented a counter nobody read.  The BNO085's tap is an
// interrupt on the board and belongs to the sensor driver when that exists (§12.0.2); until
// then this is the same arrangement `ui` already has with the knob and the cell, which is to
// diff a HAL counter on its own tick.  MOVE IT when the driver lands: the handler does not
// change, only who posts to it.  The counter is monotonic, so a missed poll costs no taps.
void Ui::poll_tap() noexcept {
    if (++tap_div_ < kTapEveryTicks) return;
    tap_div_ = 0;
    const auto s = hal::imu::read();
    if (!s.ok()) return;
    if (s.v.taps != taps_last_) {
        taps_last_ = s.v.taps;
        post(Tap{});
    }
}

// Which way up the cube is sitting (§6.1d).  Homing finds where the HANDS are; this finds
// where the DIAL is, and they are different questions -- the movement can know its index
// perfectly while the whole clock lies on its side.
//
// Same borrowed arrangement as the tap above, and it moves to `board` with it (§12.0.2): the
// BNO085 is an I2C device on somebody else's bus and this poll is a stand-in for a gravity
// report the sensor hub can be told to send by itself.  What is NOT a stand-in is everything
// after `level_.update()` -- the dead zone, the hysteresis and the confirmation count are the
// feature, and they are in domain/level.hpp where a test can turn a cube over ten thousand
// times a second.
//
// The tick is re-posted on every poll rather than only when it changes.  `motion` drops a
// repeat before it does any work, and this way there is exactly one rule -- the dial ends up
// wherever gravity last said -- instead of a second one about who re-synchronises what after
// `motion tune level` has been off.
void Ui::poll_level() noexcept {
    const uint64_t now = port::now_us();
    const uint32_t every = plugged_ ? kLevelPluggedMs : kLevelBatteryMs;
    // `level_polled_` rather than a zero timestamp: on the host the first tick really can
    // land on time zero, and testing the clock against 0 polled that tick twice.
    if (level_polled_ && now - level_at_us_ < every * 1000ull) return;
    level_polled_ = true;
    level_at_us_ = now;
    const auto s = hal::imu::read();
    // No sensor, no opinion.  The dial keeps whatever it has, which for a clock that has
    // never had an IMU fitted is the printed 12 -- exactly as it was before this existed.
    if (!s.ok()) return;
    if (level_.update(s.v.gx, s.v.gy, s.v.gz)) {
        const auto lv = level_.level();
        CLK_LOGI(ui, "level: up is %.0f deg round the dial, tilt %.2f%s -> tick %d",
                 static_cast<double>(lv.up), static_cast<double>(lv.tilt),
                 lv.flat ? " (flat -- the printed 12 it is)" : "", lv.tick);
    }
    if (motion_) motion_->set_dial_tick(level_.tick());
}

// The low-cell warning is the one thing on the pixels that no INPUT causes.  Poll it slowly
// -- on the board this is an ADC read, and four times a second is plenty for a battery.
void Ui::watch_battery() noexcept {
    if (++power_div_ < kPowerEveryTicks) return;
    power_div_ = 0;
    const auto p = hal::power::read();
    batt_warn_ = p.ok() && p.v.soc_pct < kLowBattPct && !p.v.plugged;
    if (p.ok()) plugged_ = p.v.plugged;  // ... and that paces the gravity poll above
}

// PCNT is hardware quadrature with a glitch filter; there is no ISR, we diff the count
// (§3.3).  The switch is an IRQ on target -- here the same 20 ms poll sees it.
void Ui::poll_knob() noexcept {
    const auto k = hal::knob::read();
    if (!k.ok()) return;

    if (k.v.count != knob_last_) {
        const int32_t d = k.v.count - knob_last_;
        knob_last_ = k.v.count;
        rotate(d);
    }
    if (k.v.sw != sw_last_) {
        sw_last_ = k.v.sw;
        if (k.v.sw) {
            sw_down_us_ = port::now_us();
        } else {
            press(static_cast<uint32_t>((port::now_us() - sw_down_us_) / 1000ull));
        }
    }
    if (!k.v.sw) return;

    // A finger on the knob is input, so the five-second timeout must not fire underneath a
    // deliberate ten-second hold.
    last_input_us_ = port::now_us();
    const auto t = tuning();
    if (!pair_armed_ && mode_ != Mode::Pairing &&
        port::now_us() - sw_down_us_ >= t.pair_press_ms * 1000ull) {
        // The hold commits HERE, at the ten-second mark, and lights up to say so -- a
        // gesture with no feedback until you let go is a gesture nobody discovers.  The
        // release that follows is spent (see press()).
        pair_armed_ = true;
        CLK_LOGI(ui, "held %" PRIu32 " ms -> BLE pairing", t.pair_press_ms);
        enter(Mode::Pairing);
    }
}

void Ui::enter(Mode m) noexcept {
    // The one refusal in the whole HSM (README §12).  It lives here rather than in press()
    // so that every route in -- the knob, `ui mode clock`, the app -- obeys it.
    if (m == Mode::Clock && net_owns_time()) {
        CLK_LOGW(ui, "clock: the network owns the time -- refused");
        arm(over_, kClockPx, domain::flash(domain::kRed, level(), 3));
        m = Mode::Volume;
    }
    // Leaving `clock` is what writes the time; there is exactly one way out of that mode
    // that does not, and it is the refusal above (which never entered it).
    if (mode_ == Mode::Clock && m != Mode::Clock) commit_clock();
    if (mode_ == Mode::Volume && m != Mode::Volume) chime_stop();

    mode_ = m;
    last_input_us_ = port::now_us();
    // Part of a turn left over from the last mode is not part of this one.
    counts_resid_ = 0;
    arm_resid_ = 0;
    last_unit_us_ = 0;  // ... and the first turn in a mode lands straight away

    if (m == Mode::Alarm) set_min_of_day_ = alarm_min_of_day_;
    if (m == Mode::Clock) {
        // Start from the time the clock is keeping -- and from 12:00 when nothing has ever
        // told it one.  chrono's hour and minute are an offset from an epoch it never had, so
        // an unset clock reads as minutes-since-boot: 00:04 is not a time, it is an uptime,
        // and the hands would open the mode pointing at it.
        set_min_of_day_ = 0;
        if (chrono_) {
            const auto c = chrono_->snapshot();
            if (c.valid) set_min_of_day_ = c.hour * 60 + c.minute;
        }
    }
    if (m == Mode::Volume) chime_at_us_ = port::now_us();

    // The hands are the readout in every mode but Idle and Pairing, so the clock has to let
    // go of them first: chrono pushes a fresh target every second, and it would take the
    // preview back between one turn of the knob and the next.
    if (chrono_) chrono_->set_follow(m == Mode::Idle || m == Mode::Pairing);
    show_hands();
    CLK_LOGI(ui, "mode %s", name_of(m));
}

// 64 CPR optical, no detent: sensitivity is entirely a firmware mapping.  The curve belongs
// to the VOLUME now -- a gauge 300 degrees end to end can be crossed in one spin without any
// hand being asked to be in two places.  Setting a time does not use it (§6.6d).
int32_t Ui::gain_for(int32_t mag) const noexcept {
    const auto t = tuning();
    const int32_t top = t.accel_factor > 1 ? t.accel_factor : 1;
    if (mag <= t.slow_max) return 1;
    if (t.fast_at <= t.slow_max || mag >= t.fast_at) return top;
    return 1 + (mag - t.slow_max) * (top - 1) / (t.fast_at - t.slow_max);
}

void Ui::rotate(int32_t counts) noexcept {
    if (counts == 0) return;
    last_input_us_ = port::now_us();
    // A turn in Idle wakes nothing -- press first.  A turn while pairing is not a control
    // either; there is nothing on that screen to adjust.
    if (mode_ == Mode::Idle || mode_ == Mode::Pairing) return;

    const auto t = tuning();

    if (mode_ == Mode::Bell) {
        // Direction, not distance: clockwise arms, anticlockwise disarms, and how far you
        // turned makes no difference at all.  The deadband is because an optical encoder
        // with no detent will report a count for a knock on the table.
        arm_resid_ += counts;
        if (std::abs(arm_resid_) < t.arm_deadband) return;
        const bool want = arm_resid_ > 0;
        arm_resid_ = 0;
        if (want == alarm_armed_) return;
        alarm_armed_ = want;
        CLK_LOGI(ui, "alarm %s", want ? "ON" : "OFF");
        show_hands();
        return;
    }

    // Carry the remainder.  A hand on the knob does not deliver its counts in one lump: PCNT
    // is read every tick and the UI streams a dragged knob as one- and two-count deltas, so
    // dividing each delta on its own threw away the whole turn whenever counts_per_minute
    // was more than 1 -- the knob moved nothing at all, at any speed, unless you flicked it.
    const int32_t per = t.counts_per_minute ? t.counts_per_minute : 1;

    if (mode_ == Mode::Volume) {
        // The gauge keeps the acceleration curve: it is 300 degrees end to end and cannot
        // wrap, so 0 to 100 % in one spin is a feature there rather than a hand asked to be
        // in two places (§6.6d).
        counts_resid_ += counts * gain_for(std::abs(counts));
        const int32_t units = counts_resid_ / per;  // truncates toward zero, both signs
        counts_resid_ -= units * per;
        if (units == 0) return;
        const int v = static_cast<int>(volume_) + units;
        volume_ = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
        hal::audio::set_volume_pct(volume_);
        chime_at_us_ = port::now_us();  // and hear the new level straight away
        show_hands(units > 0 ? 1 : -1);
        return;
    }

    // Alarm and Clock: hold the counts and let the tick spend them one minute at a time, no
    // faster than the hands can render them (see drain_setting).  No acceleration curve here
    // -- §6.6d's twelvefold gain made a single 20 ms poll worth two hours of dial, which the
    // minute hand cannot be asked to draw.
    counts_resid_ += counts;
    drain_setting();  // ... and let the first minute land now rather than on the next tick
}

// One minute of dial at the speed the movement is actually set to.  Exactly that, now that
// the surplus is dropped rather than banked: every millisecond of margin here is a minute of
// somebody's turn thrown away, and a hand that has to accelerate into the first minute is
// one or two behind for the rest of the wind -- a tenth of a second of tail, once.
uint32_t Ui::pace_ms() const noexcept {
    int32_t v = motion_ ? motion_->tuning().v_max : 6000;
    if (v < 1) v = 1;
    const auto ms = static_cast<uint32_t>(1000ll * domain::kRev / 60 / v);
    return ms < kPaceFloorMs ? kPaceFloorMs : (ms > kPaceCeilMs ? kPaceCeilMs : ms);
}

// The dial is the readout, so the SETTING may not move faster than the hands can show it --
// and what it cannot show, it does not keep.
//
// v_max is 6000 usteps/s and a minute of dial is 288 of them, so the movement can draw about
// twenty minutes of dial a second.  Beyond that the number is racing a hand that is nowhere
// near it, and the result is not slightly wrong but meaningless: once the setting is more
// than half a turn ahead of the minute hand there is no answer to "which way round", the
// target wraps, a hand in flight gets re-aimed at somewhere it has already passed -- so it
// stops and backs up -- and one steady turn produces a minute hand that stutters, reverses,
// or (at exactly an hour a poll) does not move at all while the hour hand sails on. That last
// one is what "the minute hand follows the hour hand" looks like from the outside (§16c).
//
// So: one minute per release, no faster than the hands run, and counts that arrive faster
// than that are DROPPED (changed 2026-08-17; they used to bank up to two seconds of winding
// and pay it out afterwards).  Banking made a fast spin worth every minute you spun it, and
// the price was a dial that carried on winding for a second or two after the knob stopped --
// a hundred and eighty degrees of minute hand, arriving somewhere you did not choose.  You
// cannot both spin faster than the hands can draw AND stop when they do; of the two, the one
// worth keeping is that what the dial says is what you set.
//
// The sub-minute remainder is still carried, so a deliberate turn loses nothing: a dragged
// knob arrives as a stream of one- and two-count deltas, and dividing each on its own would
// throw away the whole turn.
void Ui::drain_setting() noexcept {
    if (mode_ != Mode::Alarm && mode_ != Mode::Clock) return;
    const int32_t per = tuning().counts_per_minute ? tuning().counts_per_minute : 1;
    const uint64_t now = port::now_us();
    const uint64_t pace_us = pace_ms() * 1000ull;
    const int32_t held = counts_resid_ / per;  // whole minutes, signed
    if (held == 0) {
        // Nothing waiting.  Hold exactly ONE minute of credit: the first detent of a turn
        // lands the moment it arrives, which is what makes the knob feel connected -- and no
        // more than one, or an idle minute would buy a jump as soon as it is touched again.
        // The bank empties every call now, so this cap is what keeps the pace a pace: a
        // blanket reset here would hand out a free minute on every tick of a live turn.
        if (now - last_unit_us_ > pace_us) last_unit_us_ = now - pace_us;
        return;
    }
    // How many minutes the hands have had TIME to draw since the last release.  Elapsed
    // rather than one-per-call, so the rate is the rate whatever the tick is doing -- under
    // `sim warp` a single 20 ms poll is several hundred milliseconds of dial.
    const auto allow = static_cast<int32_t>((now - last_unit_us_) / pace_us);

    const int dir = held > 0 ? 1 : -1;
    const int32_t mag = held > 0 ? held : -held;
    const int32_t units = mag < allow ? mag : allow;
    // Spend what the hands can draw, and drop the rest -- except for ONE DETENT'S WORTH.
    //
    // A detent is the smallest thing a person does to this knob, and it is worth whatever the
    // sensitivity says it is worth: at the shipping four counts a minute that is one minute
    // (so this keeps nothing the strict rule would not), and at `ui knob counts 1` it is four.
    // Splitting one would make the sensitivity setting a lie -- turn the knob one notch, get a
    // quarter of what it promised -- and the whole of it is a fifth of a second of hand.  A
    // QUEUE of detents still cannot accumulate: this is a cap, not a bank.
    const int32_t detent = kDetentCounts / per > 1 ? kDetentCounts / per : 1;
    const int32_t left = mag - units;
    const int32_t keep = left < detent ? left : detent;
    counts_resid_ -= dir * (mag - keep) * per;
    if (units == 0) return;
    last_unit_us_ += static_cast<uint64_t>(units) * pace_us;  // += keeps the average exact
    // ... but credit never accumulates while the knob is still: a minute of not turning must
    // not buy a free minute the instant it is touched again.
    if (now - last_unit_us_ > pace_us) last_unit_us_ = now - pace_us;
    set_min_of_day_ = wrap_day(set_min_of_day_ + dir * units);
    if (mode_ == Mode::Alarm) alarm_min_of_day_ = set_min_of_day_;
    // The hands are still moving to what the knob asked for, so the mode is not idle -- a
    // five-second timeout that fired while the dial was visibly winding would be measured
    // from the wrong thing.
    last_input_us_ = now;
    // The hands follow the KNOB, not the shorter arc: one minute forward is one minute
    // forward even at the half hour, where the shorter arc is fifty-nine minutes back (§6.6e).
    show_hands(dir);
}

void Ui::press(uint32_t held_ms) noexcept {
    last_input_us_ = port::now_us();

    // The ten-second hold already acted while the knob was still down.  Letting go of it is
    // not also a long press, and it is certainly not the next mode.
    if (pair_armed_) {
        pair_armed_ = false;
        return;
    }

    if (held_ms >= tuning().long_press_ms) {
        CLK_LOGI(ui, "long press -> idle");
        enter(Mode::Idle);  // commits a clock set on the way out
        return;
    }

    switch (mode_) {
        case Mode::Idle:
            enter(Mode::Bell);
            break;
        case Mode::Bell:
            enter(Mode::Alarm);
            break;
        case Mode::Alarm:
            enter(Mode::Clock);
            break;
        case Mode::Clock:
            enter(Mode::Volume);
            break;
        case Mode::Volume:
        case Mode::Pairing:
            enter(Mode::Idle);
            break;
    }
}

void Ui::commit_clock() noexcept {
    if (!chrono_) return;
    const auto c = chrono_->snapshot();
    const int64_t day = c.epoch_ms - ((c.hour * 60 + c.minute) * 60 + c.second) * 1000ll;
    chrono_->set_time(day + set_min_of_day_ * 60'000ll);
}

// `net` (§6.7) does not exist yet, so the two facts it will report live on `chrono` -- which
// is the right owner anyway, being the time authority.  The rear toggle is hardware and is
// read here directly.
bool Ui::net_owns_time() const noexcept {
    const auto r = hal::expander::get(hal::expander::Sig::RadioOff);
    // The pin idles HIGH through the expander pull-up and the rear toggle pulls it LOW, so
    // a low reading is "radios disabled" and an unreadable expander fails to radios-enabled,
    // exactly as the harness does (README §16d).
    if (r.ok() && !r.v) return false;
    if (!chrono_) return false;
    const auto c = chrono_->snapshot();
    return c.net_provisioned && c.net_synced;
}

// The hands ARE the readout (README §5): the alarm time, the time being set, or the volume.
//
// `dir` is which way the knob just turned, and it is passed straight through to motion --
// see Motion::resolve.  Zero means "this is a jump between two readouts, take the short way":
// entering a mode, or arming the alarm, where there is no turn to follow.
void Ui::show_hands(int dir) noexcept {
    if (!motion_) return;
    int show = -1;
    switch (mode_) {
        case Mode::Bell:
            // Armed, the dial shows when it will go off.  Disarmed, both hands go to the 6 --
            // stacked, which is a reading no working clock can produce, so the dial is
            // visibly saying something rather than displaying a plausible wrong time.
            if (!alarm_armed_) {
                motion_->goto_usteps(kSouth, kSouth, true, dir);
                return;
            }
            show = alarm_min_of_day_;
            break;
        case Mode::Alarm:
        case Mode::Clock:
            show = set_min_of_day_;
            break;
        case Mode::Volume: {
            const int32_t u = static_cast<int32_t>(volume_) * kUstepsPerVolPct;
            motion_->goto_usteps(u, u, true, dir);
            return;
        }
        default:
            return;  // Idle and Pairing: chrono has the hands back
    }
    const auto p = domain::for_time(show / 60, show % 60);
    motion_->goto_usteps(p.hour, p.minute, true, dir);
}

// ---- light ---------------------------------------------------------------------------------

uint8_t Ui::level() const noexcept {
    const auto t = tuning();
    return static_cast<uint8_t>(255u * t.brightness / 100u);
}

// Modes 1 and 2 answer the same question -- is the alarm on? -- so they say it the same way.
//
// Both states BREATHE, and the answer is the colour: red for armed, white for off.  A fast
// blink reads as an alarm going off rather than an alarm that is set, and this is a bedroom
// -- the light on the thing you look at last is not the place for something urgent.  Same
// curve, same period, one difference, which is also what makes the pair comparable at a
// glance (§6.6b, changed 2026-08-15).
domain::Anim Ui::alarm_cue() const noexcept {
    const uint8_t l = level();
    return domain::breathe(alarm_armed_ ? domain::kRed : domain::kWhite, l);
}

// Arm an animation, but only if it is actually a DIFFERENT one.  This is the whole trick
// behind five pixels breathing in sync: cue() runs 50 times a second, and re-arming an
// unchanged cue would pin every animation to t=0 forever -- a breath would never get past
// its first millisecond, and a burst would never end.
void Ui::arm(domain::Anim* layer, std::size_t i, domain::Anim a) noexcept {
    if (domain::same(layer[i], a)) return;
    a.t0_us = port::now_us();
    layer[i] = a;
}

void Ui::fade_out(std::size_t i) noexcept {
    auto& a = base_[i];
    if (a.pattern == domain::Pattern::Off) return;
    if (a.pattern == domain::Pattern::RampDown) {
        // Already on its way out.  Re-arming it every tick from its own falling level would
        // make the fade an asymptote that never quite arrives, and "0 light when idle" (R2)
        // is not a limit, it is a value.
        if (domain::done(a, anim_, port::now_us())) a = domain::off();
        return;
    }
    arm(base_, i, domain::ramp_down(a.color, domain::envelope(a, anim_, port::now_us())));
}

// What the MODE wants each pixel to be doing.  This table is the UX (README §12).
void Ui::cue() noexcept {
    domain::Anim want[hal::pixels::kCount]{};  // Off unless something below says otherwise
    const uint8_t l = level();

    switch (mode_) {
        case Mode::Idle:
            break;  // zero emission when idle -- hard invariant (R2/R6)
        case Mode::Bell:
            want[kBell] = alarm_cue();
            break;
        case Mode::Alarm:
            want[kAlarmPx] = alarm_cue();
            break;
        case Mode::Clock:
            want[kClockPx] = domain::ramp_up(domain::kWhite, l);
            break;
        case Mode::Volume:
            want[kVol] = domain::ramp_up(domain::kWhite, l);
            break;
        case Mode::Pairing:
            // All five, armed in the same pass, so they share a t0 and breathe as one.
            for (std::size_t i = kBell; i <= kBatt; ++i)
                want[i] = domain::breathe(domain::kBlue, l);
            break;
    }

    // The cell warning is the one emitter no input causes, and the one documented exception
    // to zero-emission-when-idle: a clock that dies in the night without saying so is worse
    // than an amber pixel.  Pairing owns the whole row, so it wins for those two minutes.
    if (batt_warn_ && mode_ != Mode::Pairing) want[kBatt] = domain::breathe(domain::kAmber, l);

    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        if (want[i].pattern == domain::Pattern::Off) {
            fade_out(i);
        } else {
            arm(base_, i, want[i]);
        }
    }
}

// One pass over both layers, once a tick.  The chain is only written when the frame we
// computed differs from the frame we last wrote -- which keeps the SPI quiet on an idle
// clock AND leaves `ui led` (§9.3) in possession of a pixel nothing is animating.
void Ui::render() noexcept {
    const uint64_t now = port::now_us();
    Rgbw frame[hal::pixels::kCount];
    bool changed = force_write_;

    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        if (over_[i].pattern != domain::Pattern::Off && domain::done(over_[i], anim_, now)) {
            over_[i] = domain::off();  // transient finished; the mode gets its pixel back
        }
        auto const& a = over_[i].pattern != domain::Pattern::Off ? over_[i] : base_[i];
        frame[i] = domain::render(a, anim_, now);
        changed = changed || !(frame[i] == shown_[i]);
    }
    if (!changed) return;

    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        shown_[i] = frame[i];
        hal::pixels::set(i, frame[i]);
    }
    hal::pixels::refresh();
    force_write_ = false;
}

// ---- sound -----------------------------------------------------------------------------------
// A stand-in for `audio` (§6.2), same arrangement as the tap counter above: the gesture is
// real and worth tuning now, the ownership moves when the AO lands.  MOVE IT then -- `ui`
// has no business touching I2S once something else does.
void Ui::chime_tick() noexcept {
    if (mode_ != Mode::Volume) return;
    const uint64_t now = port::now_us();
    if (chime_off_us_ && now >= chime_off_us_) {
        hal::audio::enable(false);
        chime_off_us_ = 0;
    }
    if (!chime_off_us_ && chime_at_us_ && now >= chime_at_us_) {
        hal::audio::enable(true);
        chime_off_us_ = now + kChimeMs * 1000ull;
        chime_at_us_ = now + kChimeEveryMs * 1000ull;
    }
}

void Ui::chime_stop() noexcept {
    if (chime_off_us_) hal::audio::enable(false);
    chime_off_us_ = chime_at_us_ = 0;
}

void Ui::publish() noexcept {
    const auto t = tuning();
    const uint64_t now = port::now_us();
    const uint32_t limit = t.timeout_ms;
    const uint64_t since = now - last_input_us_;
    const auto left = static_cast<uint32_t>(since / 1000ull >= limit ? 0 : limit - since / 1000ull);
    // Read before the lock: net_owns_time() takes chrono's, and two services' mutexes held
    // in one order here and the other order there is the whole recipe for a deadlock.
    const bool locked = net_owns_time();
    port::Lock lk{mx_};
    snap_.mode = mode_;
    snap_.mode_name = name_of(mode_);
    snap_.alarm_armed = alarm_armed_;
    snap_.alarm_hour = alarm_min_of_day_ / 60;
    snap_.alarm_minute = alarm_min_of_day_ % 60;
    snap_.volume = volume_;
    snap_.counts_per_minute = tune_.counts_per_minute;
    snap_.idle_in_ms = mode_ == Mode::Idle ? 0 : left;
    snap_.net_locked = locked;
    snap_.held_ms = sw_last_ ? static_cast<uint32_t>((now - sw_down_us_) / 1000ull) : 0;
}

}  // namespace clk::svc
