// What active objects say to each other.                       [FIRMWARE.md §4.1, §3.5]
//
// A variant of small trivially-copyable structs (D2).  Events carry COPIES -- rule 4 -- so
// there is no shared mutable state to reason about and an event can sit in a mailbox as long
// as it likes without anybody having to keep something alive for it.
#pragma once

#include <cstdint>
#include <variant>

namespace clk {

// Hand positions are always absolute microsteps, never "step N times" (§6.1).  That is what
// makes the deep-sleep cadence (D7) a free parameter and what makes every target idempotent.
struct HandTarget {
    int32_t hour_usteps;
    int32_t minute_usteps;
    bool preview;  // knob is being turned: go now, do not smooth
    // Which way round to get there: 0 = the shortest way, +1 = clockwise, -1 = anticlockwise.
    // A clock wants the shortest way; a knob says which way it is being turned, because a
    // hand that reverses under a steady turn is wrong even when reversing is shorter (§6.6e).
    int8_t dir = 0;
    // Which frame these microsteps are in.  Normally the DIAL's -- "12:05" is 1440, and the
    // level offset (§6.1d) is added on arrival, so a tilted cube still puts the hands where
    // the printed dots now are.  `raw` says they are the MOVEMENT's own count already, which
    // is what a relative bench nudge and a target re-issued after homing both mean; adding
    // the offset to those would apply it twice.
    bool raw = false;
};

struct HomeRequest {};

// The per-unit hand calibration (§6.1): how far past the index a hand has to sit to look
// north, in microsteps, positive = clockwise.  An event rather than a setter because it moves
// the hand and carries every pending target with it, which only the owning AO may do.
struct ZeroSet {
    uint8_t hand;  // hal::motor::Hand -- core/ sits below hal/ and may not name it (§2)
    int32_t usteps;
};

// Which of the twelve dots is at the top of the dial, from the gravity vector (§6.1d).  0 is
// the printed 12 and the cube sitting upright; 3 is the cube laid on its right-hand face, so
// every target moves ninety degrees round to meet the dots where they now are.  An event
// rather than a setter for the same reason `ZeroSet` is one: it moves both hands.
struct DialTick {
    uint8_t tick;  // 0..11, thirty degrees each, clockwise
};

// "Stop moving", which is emphatically NOT `Stop` below.  `Stop` is the framework's shutdown
// event and ActiveObject::run() consumes it by leaving its loop -- so a service that posted
// `Stop` to mean "hold the hands where they are" killed its own thread instead, and the
// movement stayed dead until the next reboot.  One word, two meanings, no compiler error.
struct Halt {};

struct HomeDone {
    bool ok;
    uint32_t took_ms;  // sim time
};

struct HandState {
    int32_t hour_usteps;
    int32_t minute_usteps;
    bool moving;
    bool homed;
};

struct KnobDelta {
    int32_t counts;  // PCNT counts since the last read; 256 per revolution
};

struct KnobPress {
    bool down;
    uint32_t held_ms;  // valid on release
};

struct Tap {};

// Jump the knob HSM straight to a mode.  `ui mode <x>` and the app's buttons; a person can
// only ever get there by pressing, which is the point of having both.
struct ModeSet {
    uint8_t mode;
};

struct TimeChanged {
    int64_t epoch_ms;
};

struct PowerState {
    uint16_t vbat_mv;
    uint8_t soc_pct;
    bool plugged;
};

struct Stop {};  // shutdown, posted by stop() and swallowed by run().  See Halt.

using Event =
    std::variant<std::monostate, HandTarget, HomeRequest, ZeroSet, DialTick, Halt, HomeDone,
                 HandState, KnobDelta, KnobPress, Tap, ModeSet, TimeChanged, PowerState, Stop>;

// std::visit is avoided on purpose: with -fno-exceptions its valueless path becomes an
// abort, and get_if reads better in a handler that only cares about three of these.
template <class T>
[[nodiscard]] const T* as(Event const& e) noexcept {
    return std::get_if<T>(&e);
}

}  // namespace clk
