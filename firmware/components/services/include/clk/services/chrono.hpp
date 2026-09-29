// The time authority.                                          [FIRMWARE.md §6.4, §7.1]
//
// A monotonic wall clock, the zone that turns it into local time, and the hand targets that
// follow from it.  Exactly one thing decides what time it is and exactly one thing turns
// that into a HandTarget.
//
// The ZONE is a POSIX TZ rule (domain/tz.hpp), not an offset: a clock that sets itself from
// SNTP with no phone around still has to spring forward on its own.  San Francisco until
// somebody says otherwise.  The phone sends the rule (`chrono tz <posix> [<name>]`); a bare
// offset (`chrono tz <min>`, or `chrono time epoch <ms> <min>` disagreeing with the rule) is
// still accepted and becomes a fixed zone -- the phone owning DST, as before.
#pragma once

#include <cstdint>

#include "clk/ao.hpp"
#include "clk/domain/tz.hpp"
#include "clk/services/motion.hpp"

namespace clk::svc {

class Chrono final : public ActiveObject {
public:
    // Who set the clock last.  Shown, and it decides nothing -- except that `net` never
    // reports "synced" for anything but its own.
    enum class Source : uint8_t { None, Manual, Phone, Sntp };
    static const char* name(Source) noexcept;

    struct Snapshot {
        // UTC.  Local = epoch_ms + tz_off_min, where the offset is the zone's AT this instant.
        int64_t epoch_ms;
        int hour, minute, second;  // LOCAL -- what the hands show
        int tz_off_min;            // local = UTC + this, right now (DST included)
        bool tz_dst;               // ... and it is the daylight offset
        bool tz_set;               // somebody gave us a zone (NVS-backed); false = the default
        char tz_posix[domain::tz::kPosixMax];
        char tz_name[40];  // "America/Los_Angeles" -- a label, "" when none was given
        bool date_valid;   // epoch_ms carries a real date, not 1970-01-01
        bool valid;        // false until somebody sets it -- there is no RTC in clocksim
        bool follow;       // are the hands tracking the clock?
        Source src;
        int32_t target_hour, target_minute;
        // Who owns the time.  `ui` refuses to let the knob set the clock when the network
        // does (README §12), because SNTP would overwrite it at the next sync and the user
        // would be left thinking the knob is broken.
        bool net_provisioned;  // Wi-Fi credentials are stored
        bool net_synced;       // ... and SNTP has landed at least once
    };

    Chrono() noexcept;

    // Legacy: a LOCAL wall time written as if it were UTC, with no zone applied.  Kept for
    // the benches that drive the hands directly.
    void set_time(int64_t epoch_ms) noexcept { post(TimeChanged{epoch_ms}); }
    // The phone's answer (app/PROTOCOL.md): a real UTC instant with its date, and the UTC
    // offset in force right now.  An offset the zone agrees with changes nothing about the
    // zone; one it disagrees with replaces it with a fixed offset (the phone is travelling,
    // or knows better).
    void set_epoch(int64_t utc_ms, int tz_off_min) noexcept;
    // A UTC instant with its date, zone untouched: SNTP, and `chrono time epoch` with no offset.
    void set_utc(int64_t utc_ms, Source src) noexcept;
    void set_tz(int tz_off_min) noexcept;  // a fixed zone; keeps the instant, moves the hands
    // A POSIX rule.  False (and nothing changes) when it does not parse.  `name` is a label.
    bool set_zone(const char* posix, const char* name) noexcept;
    // A time of day from the knob or `chrono time set`, LOCAL, keeping today's date when there
    // is one.  Returns the UTC instant it posted.
    int64_t set_local_time(int h, int m, int s) noexcept;
    static constexpr int kTzMinMin = -12 * 60;
    static constexpr int kTzMaxMin = 14 * 60;
    // Reported by `net` (§6.7) on every change; `chrono net` overrides it on the bench.  Two
    // facts, not a state machine: whether the radios are actually up is the rear toggle's
    // business and `ui` reads that itself.
    void set_net(bool provisioned, bool synced) noexcept;
    void set_follow(bool on) noexcept;
    // How many distinct positions the hands take per minute of wall time: 1 ticks once a
    // minute, 60 moves every second.  A rendering choice, not a timekeeping one -- the clock
    // itself is unaffected, only how often it asks the hands to move.
    void set_steps_per_minute(int n) noexcept;
    [[nodiscard]] int steps_per_minute() const noexcept;
    [[nodiscard]] Snapshot snapshot() const noexcept;
    [[nodiscard]] int64_t now_epoch_ms() const noexcept;

    void bind(Motion* m) noexcept { motion_ = m; }

protected:
    void on_start() override;
    void on_event(Event const&) override;
    void on_tick() override;

private:
    void push_target(bool force) noexcept;
    void zone_locked(domain::tz::Zone const&, const char* posix, const char* name,
                     bool set) noexcept;
    [[nodiscard]] int off_min_locked(int64_t utc_ms) const noexcept;

    mutable port::Mutex mx_;
    Snapshot snap_{};
    Motion* motion_ = nullptr;

    // Wall time is an offset from the monotonic base, so warping sim time warps the clock
    // with it and there is no second time source to disagree.
    int64_t epoch_base_ms_ = 0;
    uint64_t mono_base_us_ = 0;
    bool valid_ = false;
    bool date_valid_ = false;  // under mx_
    domain::tz::Zone zone_{};  // under mx_ -- and snap_.tz_posix / tz_name / tz_set with it
    Source src_ = Source::None;
    bool follow_ = true;
    int steps_per_minute_ = 60;  // one position per second: a sweep, not a tick
    int32_t last_h_ = -1, last_m_ = -1;
};

Chrono& chrono() noexcept;

}  // namespace clk::svc
