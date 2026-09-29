#include "clk/services/chrono.hpp"

#include <cstdio>
#include <cstring>

#include "clk/domain/hand.hpp"
#include "clk/hal/hal.hpp"
#include "clk/log.hpp"

namespace clk::svc {
namespace {

constexpr uint32_t kTickMs = 250;  // four times a second is plenty for a minute hand
// NVS (§7.5).  The zone as its POSIX rule + a label; `chr.tz` is the fixed offset an older
// firmware stored, read once if there is no rule.
constexpr const char* kKeyTzPosix = "chr.tzp";
constexpr const char* kKeyTzName = "chr.tzn";
constexpr const char* kKeyTzLegacy = "chr.tz";

struct Hms {
    int h, m, s;
};

// Time of day of a LOCAL millisecond count (UTC + offset, already added by the caller).
Hms hms_of(int64_t epoch_ms) noexcept {
    int64_t secs = epoch_ms / 1000;
    int64_t day = secs % 86400;
    if (day < 0) day += 86400;
    return {static_cast<int>(day / 3600), static_cast<int>((day % 3600) / 60),
            static_cast<int>(day % 60)};
}

int64_t floor_div(int64_t a, int64_t b) noexcept { return domain::tz::floor_div(a, b); }

}  // namespace

const char* Chrono::name(Source s) noexcept {
    switch (s) {
        case Source::Manual:
            return "manual";
        case Source::Phone:
            return "phone";
        case Source::Sntp:
            return "sntp";
        default:
            return "none";
    }
}

Chrono::Chrono() noexcept : ActiveObject({"chrono", 12, 4096, kTickMs}) {}

Chrono& chrono() noexcept {
    static Chrono c;
    return c;
}

void Chrono::on_start() {
    mono_base_us_ = port::now_us();
    char posix[domain::tz::kPosixMax] = "";
    char label[sizeof snap_.tz_name] = "";
    domain::tz::Zone z{};
    port::Lock lk{mx_};
    if (hal::store::get_str(kKeyTzPosix, posix, sizeof posix) == Status::Ok &&
        domain::tz::parse(posix, z)) {
        (void)hal::store::get_str(kKeyTzName, label, sizeof label);
        zone_locked(z, posix, label, true);
    } else if (const auto tz = hal::store::get_i32(kKeyTzLegacy); tz.ok()) {
        domain::tz::fixed(tz.v, posix, sizeof posix);
        (void)domain::tz::parse(posix, z);
        zone_locked(z, posix, "", true);
    } else {
        (void)domain::tz::parse(domain::tz::kDefault, z);
        zone_locked(z, domain::tz::kDefault, domain::tz::kDefaultName, false);
    }
    CLK_LOGI(chrono, "up; zone %s%s%s; time not set until the phone or SNTP says", snap_.tz_posix,
             snap_.tz_name[0] ? " = " : "", snap_.tz_name);
}

void Chrono::zone_locked(domain::tz::Zone const& z, const char* posix, const char* label,
                         bool set) noexcept {
    zone_ = z;
    std::snprintf(snap_.tz_posix, sizeof snap_.tz_posix, "%s", posix ? posix : "");
    std::snprintf(snap_.tz_name, sizeof snap_.tz_name, "%s", label ? label : "");
    snap_.tz_set = set;
    last_h_ = last_m_ = -1;  // same instant, possibly different hands
}

int Chrono::off_min_locked(int64_t utc_ms) const noexcept {
    return domain::tz::offset_s(zone_, floor_div(utc_ms, 1000)) / 60;
}

void Chrono::set_epoch(int64_t utc_ms, int tz_off_min) noexcept {
    {
        port::Lock lk{mx_};
        if (off_min_locked(utc_ms) != tz_off_min) {
            char posix[domain::tz::kPosixMax];
            domain::tz::fixed(tz_off_min, posix, sizeof posix);
            domain::tz::Zone z{};
            (void)domain::tz::parse(posix, z);
            zone_locked(z, posix, "", true);
            (void)hal::store::set_str(kKeyTzPosix, posix);
            (void)hal::store::set_str(kKeyTzName, "");
        }
        date_valid_ = true;
    }
    post(TimeChanged{utc_ms, static_cast<uint8_t>(Source::Phone)});
}

void Chrono::set_utc(int64_t utc_ms, Source src) noexcept {
    {
        port::Lock lk{mx_};
        date_valid_ = true;
    }
    post(TimeChanged{utc_ms, static_cast<uint8_t>(src)});
}

void Chrono::set_tz(int tz_off_min) noexcept {
    char posix[domain::tz::kPosixMax];
    domain::tz::fixed(tz_off_min, posix, sizeof posix);
    (void)set_zone(posix, "");
}

bool Chrono::set_zone(const char* posix, const char* label) noexcept {
    domain::tz::Zone z{};
    if (!posix || std::strlen(posix) >= domain::tz::kPosixMax || !domain::tz::parse(posix, z))
        return false;
    {
        port::Lock lk{mx_};
        zone_locked(z, posix, label, true);
    }
    (void)hal::store::set_str(kKeyTzPosix, posix);
    (void)hal::store::set_str(kKeyTzName, label ? label : "");
    CLK_LOGI(chrono, "zone %s%s%s", posix, label && *label ? " = " : "", label ? label : "");
    return true;
}

int64_t Chrono::set_local_time(int h, int m, int s) noexcept {
    const int64_t tod_s = (h * 60LL + m) * 60 + s;
    int64_t utc;
    {
        port::Lock lk{mx_};
        int64_t day_s = 0;  // local midnight, as local seconds since the epoch
        if (valid_) {
            // Today, locally: the local date of the current instant.
            const int64_t local = snap_.epoch_ms + off_min_locked(snap_.epoch_ms) * 60'000LL;
            day_s = floor_div(local, 86'400'000LL) * 86'400;
        }
        // No date to keep: 1970-01-01, and date_valid says so.
        utc = domain::tz::local_to_utc(zone_, day_s + tod_s) * 1000;
    }
    post(TimeChanged{utc, static_cast<uint8_t>(Source::Manual)});
    return utc;
}

int64_t Chrono::now_epoch_ms() const noexcept {
    port::Lock lk{mx_};
    return snap_.epoch_ms;
}

Chrono::Snapshot Chrono::snapshot() const noexcept {
    port::Lock lk{mx_};
    return snap_;
}

// Called from OTHER threads -- `ui` on every mode change, `cli` on `chrono follow`.  All
// three fields are read by push_target() on chrono's own thread, so all three are written
// under the mutex.  They were not, and ThreadSanitizer called it exactly right: a stale
// `follow_` leaves the clock driving the hands through a knob preview, which is a bug you
// would chase in `ui` for an hour (FIRMWARE.md §16).
void Chrono::set_follow(bool on) noexcept {
    port::Lock lk{mx_};
    snap_.follow = on;
    follow_ = on;
    last_h_ = last_m_ = -1;  // force a push on the next tick
}

void Chrono::set_net(bool provisioned, bool synced) noexcept {
    port::Lock lk{mx_};
    // Straight into the snapshot: on_tick rewrites the time fields and leaves these alone,
    // and nothing inside chrono reads them -- they exist to be asked about.
    snap_.net_provisioned = provisioned;
    snap_.net_synced = synced;
}

void Chrono::set_steps_per_minute(int n) noexcept {
    port::Lock lk{mx_};
    steps_per_minute_ = n < 1 ? 1 : (n > 60 ? 60 : n);
    last_h_ = last_m_ = -1;  // the current position is quantised differently now
}

int Chrono::steps_per_minute() const noexcept {
    port::Lock lk{mx_};
    return steps_per_minute_;
}

void Chrono::on_event(Event const& e) {
    if (const auto* t = as<TimeChanged>(e)) {
        const bool was_valid = valid_;
        const int64_t before =
            epoch_base_ms_ + static_cast<int64_t>((port::now_us() - mono_base_us_) / 1000ull);
        epoch_base_ms_ = t->epoch_ms;
        mono_base_us_ = port::now_us();
        valid_ = true;
        const auto src = static_cast<Source>(t->src);
        int off;
        {
            port::Lock lk{mx_};
            src_ = src;
            off = off_min_locked(t->epoch_ms);
        }
        const auto hm = hms_of(t->epoch_ms + off * 60'000LL);
        if (src == Source::Sntp && was_valid) {
            CLK_LOGI(chrono, "sntp: %02d:%02d:%02d local, stepped %+lld ms", hm.h, hm.m, hm.s,
                     static_cast<long long>(t->epoch_ms - before));
        } else {
            CLK_LOGI(chrono, "time set to %02d:%02d:%02d local (UTC%+d min, %s)", hm.h, hm.m, hm.s,
                     off, name(src));
        }
        push_target(true);
        return;
    }
    if (const auto* h = as<HomeDone>(e)) {
        // A fresh zero means the hands are somewhere we did not ask for; put them back on
        // the time rather than waiting for the minute to roll over.
        CLK_LOGI(chrono, "motion homed (%s)", h->ok ? "ok" : "failed");
        if (h->ok) push_target(true);
    }
}

void Chrono::on_tick() {
    const int64_t now_ms =
        epoch_base_ms_ + static_cast<int64_t>((port::now_us() - mono_base_us_) / 1000ull);
    int steps, off;
    bool dst;
    {
        port::Lock lk{mx_};
        steps = steps_per_minute_;
        off = off_min_locked(now_ms);
        dst = zone_.has_dst && off * 60 == zone_.dst_off_s;
    }
    const auto hm = hms_of(now_ms + off * 60'000LL);
    const auto p = domain::for_time(hm.h, hm.m, hm.s, steps);
    {
        port::Lock lk{mx_};
        snap_.epoch_ms = now_ms;
        snap_.hour = hm.h;
        snap_.minute = hm.m;
        snap_.second = hm.s;
        snap_.tz_off_min = off;
        snap_.tz_dst = dst;
        snap_.date_valid = date_valid_ && valid_;
        snap_.valid = valid_;
        snap_.follow = follow_;
        snap_.src = src_;
        snap_.target_hour = p.hour;
        snap_.target_minute = p.minute;
    }
    push_target(false);
}

// Absolute targets, only when they change: the movement is idle >99 % of the time (§6.1) and
// re-sending the same position every tick would keep the coils alive for nothing.
void Chrono::push_target(bool force) noexcept {
    if (!motion_) return;
    int32_t h, m;
    {
        // One acquisition, and snap_ read directly rather than through snapshot(): the
        // decision and the fields it is made from have to come from the same instant, and
        // `port::Lock` is not recursive.
        port::Lock lk{mx_};
        if (!valid_ || !follow_) return;
        h = snap_.target_hour;
        m = snap_.target_minute;
        if (!force && h == last_h_ && m == last_m_) return;
        last_h_ = h;
        last_m_ = m;
    }
    motion_->goto_usteps(h, m);  // outside the lock: it posts to another AO's queue
}

}  // namespace clk::svc
