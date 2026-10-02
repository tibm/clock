#include "clk/services/supervisor.hpp"

#include <cinttypes>
#include <cstdio>

#include "clk/hal/hal.hpp"
#include "clk/journal.hpp"
#include "clk/log.hpp"

namespace clk::svc {
namespace {

// Above motion (20): see the header.  Its work is a few snapshot reads and a log line a second.
constexpr int kPrio = 21;
constexpr std::size_t kStack = 4096;
constexpr uint64_t kSec = 1'000'000;
constexpr uint64_t kFirstBeatUs = 10 * kSec;  // one early, so a short boot still has one

uint32_t secs(uint64_t us) noexcept { return static_cast<uint32_t>(us / kSec); }

}  // namespace

Supervisor::Supervisor() noexcept : ActiveObject({"sup", kPrio, kStack, kTickMs}) {}

Supervisor& supervisor() noexcept {
    static Supervisor s;
    return s;
}

void Supervisor::bind(Motion* m, Chrono* c, Storage* s, Ui* u) noexcept {
    motion_ = m;
    chrono_ = c;
    storage_ = s;
    ui_ = u;
}

const char* Supervisor::fault_name(uint8_t bit) noexcept {
    switch (bit) {
        case kFaultHands:
            return "hands";
        case kFaultCharger:
            return "charger";
        case kFaultAmp:
            return "amp";
        default:
            return "?";
    }
}

uint8_t Supervisor::ack() noexcept {
    port::Lock lk{mx_};
    const uint8_t was = snap_.faults_shown;
    acked_ = snap_.faults_active;  // hidden while it lasts; a raise clears its bit again
    snap_.faults_latched = snap_.faults_active;
    snap_.faults_shown = 0;
    return was;
}

void Supervisor::watch(ActiveObject* ao) noexcept {
    if (ao && n_aos_ < kMaxWatched) aos_[n_aos_++] = ao;
}

Supervisor::Snapshot Supervisor::snapshot() const noexcept {
    port::Lock lk{mx_};
    return snap_;
}

void Supervisor::on_start() {
    const uint64_t now = port::now_us();
    moved_us_ = now;
    next_beat_us_ = now + kFirstBeatUs;
    {
        port::Lock lk{mx_};
        snap_.restart = restart_;
    }
    CLK_LOGI(sup, "up; watching %zu AO(s), hands stall after %" PRIu32 " s%s", n_aos_, kHandsStallS,
             restart_ ? ", restart if it persists" : "");
}

void Supervisor::on_tick() {
    const uint64_t now = port::now_us();
    check_aos(now);  // first, and lock-free: a hung AO may be holding its snapshot's mutex
    check_hands(now);
    check_faults();
    if (now >= next_beat_us_ || next_beat_us_ - now > kHeartbeatS * kSec) {
        next_beat_us_ = now + kHeartbeatS * kSec;
        heartbeat();
    }
}

void Supervisor::check_aos(uint64_t now) noexcept {
    for (std::size_t i = 0; i < n_aos_; ++i) {
        ActiveObject* ao = aos_[i];
        const uint64_t seen = ao->alive_us();
        if (!ao->running() || !seen || ao->paused()) {
            ao_seen_[i] = 0;
            continue;
        }
        // Stuck = the stamp has not moved since the last tick AND is old.  The first half is
        // what keeps a `sim jump` (sim time leaps, every stamp is suddenly "old", and every
        // loop re-stamps within a real millisecond) from reading as five hung AOs.
        const bool same = seen == ao_seen_[i];
        ao_seen_[i] = seen;
        const uint64_t age = now > seen ? now - seen : 0;
        if (same && age > kAoStallMs * 1000ull) {
            if (ao_stuck_[i]) continue;
            ao_stuck_[i] = true;
            {
                port::Lock lk{mx_};
                ++snap_.ao_stalls;
            }
            CLK_LOGE(sup,
                     "AO %s stuck: its loop has not come round for %" PRIu32 " ms (handled %" PRIu32
                     ", dropped %" PRIu32 ")%s",
                     ao->name(), static_cast<uint32_t>(age / 1000u), ao->handled(), ao->dropped(),
                     ao->watched() ? "; the task watchdog resets at 10 s" : "");
        } else if (!same && ao_stuck_[i]) {
            ao_stuck_[i] = false;
            CLK_LOGW(sup, "AO %s running again", ao->name());
        }
    }
}

void Supervisor::check_hands(uint64_t now) noexcept {
    if (!motion_ || !chrono_) return;
    const auto m = motion_->snapshot();
    const auto c = chrono_->snapshot();
    const bool expect = c.valid && c.follow && m.homed &&
                        (m.state == Motion::State::Idle || m.state == Motion::State::Moving) &&
                        motion_->accepts(false) == Status::Ok;

    if (m.state == Motion::State::Moving) {
        if (!moving_since_us_) moving_since_us_ = now;
    } else {
        moving_since_us_ = 0;
    }
    if (!expect || m.hour != last_h_ || m.minute != last_m_) {
        last_h_ = m.hour;
        last_m_ = m.minute;
        moved_us_ = now;
        demands_ = 0;
    }
    if (c.target_hour != last_th_ || c.target_minute != last_tm_) {
        last_th_ = c.target_hour;
        last_tm_ = c.target_minute;
        if (expect) ++demands_;
    }

    const char* why = nullptr;
    if (expect && demands_ >= 2 && now - moved_us_ >= kHandsStallS * kSec) {
        why = "the hands stopped following the time";
    } else if (moving_since_us_ && now - moving_since_us_ >= kMoveStallS * kSec) {
        why = "a move that does not finish";
    }

    if (why) {
        if (!stall_since_us_) {
            stall_since_us_ = now;
            {
                port::Lock lk{mx_};
                ++snap_.hand_stalls;
            }
            // The detail -- where both hands are, where they were sent, chrono's target -- is
            // the heartbeat, logged right after.
            CLK_LOGE(sup,
                     "HANDS STALLED: %s (%" PRIu32 " target change(s) unanswered in %" PRIu32
                     " s, %" PRIu32 " motion fault(s))",
                     why, demands_, secs(now - moved_us_), m.faults);
            heartbeat();
            if (storage_) (void)storage_->dbg_flush();  // on the card now, not in 5 s
        } else if (restart_ && now - stall_since_us_ >= kRestartAfterS * kSec) {
            // The journal is in .noinit RAM: the lines above survive esp_restart, and the
            // next boot appends them to this boot's file.
            CLK_LOGE(sup, "hands still stalled after %" PRIu32 " s: restarting",
                     secs(now - stall_since_us_));
            (void)hal::reboot();
        }
    } else if (stall_since_us_) {
        CLK_LOGW(sup, "hands moving again after a %" PRIu32 " s stall",
                 secs(now - stall_since_us_));
        stall_since_us_ = 0;
    }
    port::Lock lk{mx_};
    snap_.stalled = stall_since_us_ != 0;
    snap_.stalled_s = stall_since_us_ ? secs(now - stall_since_us_) : 0;
    snap_.why = why;
}

// Once a second.  Each raw condition has to hold kFaultConfirmS ticks running before it is a
// fault -- an open-drain FAULT line read through an expander can glitch, and a blinking pixel
// all night over one bad read is worse than three seconds of latency on a real one.
void Supervisor::check_faults() noexcept {
    uint8_t raw = 0;
    if (motion_ && motion_->snapshot().state == Motion::State::Fault) raw |= kFaultHands;
    if (stall_since_us_) raw |= kFaultHands;
    if (ui_) {
        const auto u = ui_->snapshot();
        // FAULT is the charger's, so it only means something while the charger has an input.
        if (u.power_ok && u.power.plugged && u.power.fault) raw |= kFaultCharger;
    }
    // SPK_FAULT is only the amp's answer while it is out of shutdown.
    if (hal::audio::active()) {
        if (const auto f = hal::expander::get(hal::expander::Sig::SpkFault); f.ok() && !f.v)
            raw |= kFaultAmp;
    }

    uint8_t active = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        const auto bit = static_cast<uint8_t>(1u << i);
        if (raw & bit) {
            if (fault_secs_[i] < 255) ++fault_secs_[i];
        } else {
            fault_secs_[i] = 0;
        }
        if (fault_secs_[i] >= kFaultConfirmS) active |= bit;
    }

    uint8_t raised = 0, cleared = 0;
    {
        port::Lock lk{mx_};
        raised = active & ~snap_.faults_active;
        cleared = snap_.faults_active & ~active;
        acked_ &= ~raised;  // a fresh raise shows, whatever was acknowledged before
        snap_.faults_active = active;
        snap_.faults_latched |= active;
        snap_.faults_shown = snap_.faults_latched & ~acked_;
        for (uint8_t i = 0; i < 3; ++i)
            if (raised & (1u << i)) ++snap_.fault_raises;
    }
    for (uint8_t i = 0; i < 3; ++i) {
        const auto bit = static_cast<uint8_t>(1u << i);
        if (raised & bit) CLK_LOGE(sup, "FAULT %s -- latched on the status row", fault_name(bit));
        if (cleared & bit)
            CLK_LOGW(sup, "fault %s cleared (still shown until acknowledged)", fault_name(bit));
    }
}

void Supervisor::heartbeat() noexcept {
    const auto si = hal::sys::info();
    const uint64_t now = port::now_us();
    if (motion_ && chrono_) {
        const auto m = motion_->snapshot();
        const auto c = chrono_->snapshot();
        CLK_LOGI(sup,
                 "hb up %" PRIu32 "s heap %" PRIu32 "/%" PRIu32
                 " | %02d:%02d:%02d%s%s | "
                 "motion %s%s%s pos %" PRId32 "/%" PRId32 " tgt %" PRId32 "/%" PRId32
                 " | chrono %" PRId32 "/%" PRId32,
                 secs(now), si.heap_free, si.heap_min, c.hour, c.minute, c.second,
                 c.valid ? "" : " unset", c.follow ? "" : " nofollow",
                 m.state_name ? m.state_name : "?", m.phase && *m.phase ? "/" : "",
                 m.phase ? m.phase : "", m.hour, m.minute, m.target_hour, m.target_minute,
                 c.target_hour, c.target_minute);
    }
    char aos[160];
    std::size_t n = 0;
    aos[0] = '\0';
    for (std::size_t i = 0; i < n_aos_ && n < sizeof aos; ++i) {
        const uint64_t seen = aos_[i]->alive_us();
        const uint32_t age = seen && now > seen ? static_cast<uint32_t>((now - seen) / 1000u) : 0;
        const int k = std::snprintf(
            aos + n, sizeof aos - n, " %s %" PRIu32 "/%" PRIu32 "/%" PRIu32 "%s", aos_[i]->name(),
            aos_[i]->handled(), aos_[i]->dropped(), age, aos_[i]->watched() ? "" : "*");
        if (k < 0) break;
        n += static_cast<std::size_t>(k);
    }
    const auto js = journal::stats();
    CLK_LOGI(sup, "hb ao(handled/dropped/age ms, *=no wdt)%s | journal %" PRIu32 " lost %" PRIu32,
             aos, js.used, js.lost);
    port::Lock lk{mx_};
    ++snap_.beats;
}

}  // namespace clk::svc
