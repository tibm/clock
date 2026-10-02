// The supervisor: is everything still running, and are the hands still moving?  [FIRMWARE.md §6.8]
//
// The watching half of §6.8, and the fault latch.  Power policy comes later.
//
// Four jobs, once a second:
//
//   1. AO liveness.  Every watched AO stamps its loop (ActiveObject::alive_us).  One whose
//      stamp is older than kAoStallMs is inside a handler that has not returned: logged, by
//      name, while the chip is still up.  The TWDT resets it at 10 s (port::wdt_*); the line
//      is in the journal's .noinit RAM by then, so the next boot writes it to the card.
//
//   2. The hands.  An AO can be alive and the hands still stuck -- a step generator that
//      stopped, a target dropped on the floor.  Stalled = the clock wants the hands to follow
//      (chrono valid + following, motion homed and accepting targets), chrono's target has
//      moved at least twice, and the hands have not moved for kHandsStallS; or a single move
//      that has gone on for kMoveStallS.  Logged with everything the two snapshots say.  If it
//      is still stalled kRestartAfterS later and restarts are enabled (the firmware, not the
//      host), the chip restarts: a clock showing the wrong time forever is the worse outcome,
//      and the journal carries the evidence across the restart.
//
//   3. A heartbeat line every kHeartbeatS -- uptime, heap, time, both hands and their
//      targets, every AO's handled/dropped/age -- so a file on the card shows when things
//      stopped, not just that they did.
//
//   4. The fault latch (§6.6g).  Three things the user can do something about -- the hands
//      (stalled, or `motion` in Fault), the charger (LT3652 FAULT while plugged) and the amp
//      (SPK_FAULT while it is up) -- each confirmed for kFaultConfirmS, then LATCHED: `ui`
//      blinks the pixel that names it until somebody acknowledges it (a long press in idle,
//      or `sys fault ack`).  Acknowledging hides what is latched; a fault that clears and comes
//      back shows again.
//
// Priority ABOVE the AOs it watches (it only reads snapshots and logs), so a runaway AO on
// core 1 does not also silence the thing that would say so.
#pragma once

#include <cstddef>
#include <cstdint>

#include "clk/ao.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"
#include "clk/services/storage.hpp"
#include "clk/services/ui.hpp"

namespace clk::svc {

class Supervisor final : public ActiveObject {
public:
    static constexpr uint32_t kTickMs = 1000;
    static constexpr uint32_t kHeartbeatS = 60;
    static constexpr uint32_t kAoStallMs = 3000;  // the TWDT resets at 10 s
    static constexpr uint32_t kHandsStallS = 180;
    static constexpr uint32_t kMoveStallS = 120;
    static constexpr uint32_t kRestartAfterS = 300;  // of a confirmed hands stall
    static constexpr std::size_t kMaxWatched = 8;
    static constexpr uint32_t kFaultConfirmS = 3;  // a glitch on an open-drain pin is not a fault

    // The latch's bits.  Each names the status pixel that shows it (§6.6g).
    enum Fault : uint8_t {
        kFaultHands = 1u << 0,    // `clock` pixel: the dial is not showing the time
        kFaultCharger = 1u << 1,  // `batt`  pixel: LT3652 FAULT (NTC window / bad cell / timer)
        kFaultAmp = 1u << 2,      // `vol`   pixel: TAS5760M SPK_FAULT
    };
    static const char* fault_name(uint8_t bit) noexcept;

    struct Snapshot {
        uint32_t beats;          // heartbeat lines logged
        uint32_t ao_stalls;      // episodes, since boot
        uint32_t hand_stalls;    // episodes, since boot
        bool stalled;            // the hands, right now
        uint32_t stalled_s;      // ... for this long
        const char* why;         // a literal, or nullptr
        bool restart;            // a confirmed stall restarts the chip
        uint8_t faults_active;   // confirmed, right now
        uint8_t faults_latched;  // seen since the last ack (active ones included)
        uint8_t faults_shown;    // latched and not acknowledged: what the status row blinks
        uint32_t fault_raises;   // since boot
    };

    Supervisor() noexcept;

    // `u` is optional: it is where the charger's FAULT comes from (`ui` already polls the
    // power chips; a second reader is a race in a driver written for one, see `net`).
    void bind(Motion*, Chrono*, Storage*, Ui* u = nullptr) noexcept;
    void watch(ActiveObject*) noexcept;  // before start(); up to kMaxWatched
    void set_restart_on_stall(bool on) noexcept { restart_ = on; }

    // Log the two heartbeat lines now.  Any thread: it only reads snapshots.
    void heartbeat() noexcept;
    [[nodiscard]] Snapshot snapshot() const noexcept;
    // "I have seen it": hide every latched fault, and forget the ones that have cleared.  One
    // still active stays latched but hidden until it clears and is raised again.  Any thread.
    // Returns what was showing.
    uint8_t ack() noexcept;

protected:
    void on_start() override;
    void on_tick() override;

private:
    void check_aos(uint64_t now) noexcept;
    void check_hands(uint64_t now) noexcept;
    void check_faults() noexcept;

    Motion* motion_ = nullptr;
    Chrono* chrono_ = nullptr;
    Storage* storage_ = nullptr;
    Ui* ui_ = nullptr;
    ActiveObject* aos_[kMaxWatched]{};
    std::size_t n_aos_ = 0;
    bool ao_stuck_[kMaxWatched]{};
    uint64_t ao_seen_[kMaxWatched]{};  // alive_us() at the last tick
    bool restart_ = false;

    uint64_t next_beat_us_ = 0;
    // hands
    int32_t last_h_ = 0, last_m_ = 0;    // where the hands were at the last move
    int32_t last_th_ = 0, last_tm_ = 0;  // chrono's target, last seen
    uint64_t moved_us_ = 0;              // the hands last moved (or stopped being expected to)
    uint32_t demands_ = 0;               // chrono target changes since then
    uint64_t moving_since_us_ = 0;       // motion has been in Moving since, 0 = not moving
    uint64_t stall_since_us_ = 0;        // 0 = not stalled
    // faults: consecutive seconds each raw condition has held (index = bit number)
    uint8_t fault_secs_[3]{};
    uint8_t acked_ = 0;  // under mx_, with snap_'s three masks

    mutable port::Mutex mx_;
    Snapshot snap_{};
};

Supervisor& supervisor() noexcept;

}  // namespace clk::svc
