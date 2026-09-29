// `storage`, the history half: readings in, 24-byte records out, day files on the card, and
// the download to the phone.                        [FIRMWARE.md §6.3a, app/PROTOCOL.md "History"]
//
// The period is on the UTC clock (a 5-minute record starts at :00, :05, ...), so two clocks, or
// one clock before and after a reboot, write records on the same grid.  Nothing is written
// until the clock has a real date: readings before that are averaged into the first record,
// and events are held (16 of them) and back-dated when the time arrives.
//
// The card is written every 15 min, not every record: fewer FAT updates, less power on the
// cell, and a power cut costs at most 15 minutes.  Never while a file is playing -- the alarm's
// stream owns the SPI bus's attention.  With no card the RAM ring keeps the newest records
// (8192 = 28 days at 5 min) and the oldest are dropped, counted.
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <new>

#include "clk/log.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/storage.hpp"

namespace clk::svc {
namespace {

namespace hist = transport::hist;

constexpr const char* kKeyPeriod = "log.per";  // NVS
constexpr const char* kKeyKeep = "log.keep";
constexpr const char* kKeyCap = "log.cap";
constexpr const char* kKeyOn = "log.on";
constexpr const char* kLogDir = "/sd/log";

constexpr std::size_t kRingRecords = 8192;  // 192 KB -- PSRAM on target (> 4 KB malloc)
constexpr uint64_t kFlushEveryUs = 15ull * 60 * 1'000'000;
constexpr std::size_t kFlushAtRecords = 1024;  // or sooner, at a fast period
constexpr uint32_t kAssumedCluster = 32768;

uint32_t cluster_of(hal::sd::Info const& in) {
    return in.cluster_bytes ? in.cluster_bytes : kAssumedCluster;
}

struct DayFile {
    uint32_t day;  // yyyymmdd
    uint32_t size;
};

struct Collect {
    DayFile* v;
    std::size_t n, cap;
    uint32_t year;
};

bool collect_days(hal::sd::Entry const& e, void* ctx) {
    auto* c = static_cast<Collect*>(ctx);
    unsigned mmdd = 0;
    char tail[8] = "";
    if (e.dir || std::sscanf(e.name, "%4u.%3s", &mmdd, tail) != 2 ||
        std::strcmp(tail, "bin") != 0 || std::strlen(e.name) != 8)
        return true;
    if (c->n < c->cap) c->v[c->n++] = DayFile{c->year * 10000 + mmdd, e.size};
    return true;
}

bool collect_years(hal::sd::Entry const& e, void* ctx) {
    auto* years = static_cast<uint32_t*>(ctx);  // [0] = count, then up to 63 years
    unsigned y = 0;
    if (e.dir && std::strlen(e.name) == 4 && std::sscanf(e.name, "%4u", &y) == 1 && y >= 1970 &&
        years[0] < 63)
        years[++years[0]] = y;
    return true;
}

// Every day file on the card, sorted oldest first.  `v` must hold `cap` entries.
std::size_t list_days(DayFile* v, std::size_t cap) {
    uint32_t years[64] = {};
    if (hal::sd::list(kLogDir, &collect_years, years) != Status::Ok) return 0;
    Collect c{v, 0, cap, 0};
    for (uint32_t i = 1; i <= years[0]; ++i) {
        char dir[32];
        std::snprintf(dir, sizeof dir, "%s/%04u", kLogDir, static_cast<unsigned>(years[i]));
        c.year = years[i];
        (void)hal::sd::list(dir, &collect_days, &c);
    }
    std::sort(v, v + c.n, [](DayFile const& a, DayFile const& b) { return a.day < b.day; });
    return c.n;
}

void day_file_path(uint32_t yyyymmdd, char* out, std::size_t cap) {
    std::snprintf(out, cap, "%s/%04u/%04u.bin", kLogDir, static_cast<unsigned>(yyyymmdd / 10000),
                  static_cast<unsigned>(yyyymmdd % 10000));
}

}  // namespace

// ---- any thread ------------------------------------------------------------------------------

uint64_t Storage::log_projected(LogCfg const& c, uint32_t cluster) noexcept {
    return hist::projected_bytes(c.period_s, c.keep_days, cluster);
}

Storage::LogCfg Storage::log_cfg() const noexcept {
    port::Lock lk{hmx_};
    return cfg_;
}

Status Storage::log_set(LogCfg const& c, char* why, std::size_t cap) noexcept {
    if (why && cap) why[0] = '\0';
    if (c.period_s < kPeriodMin || c.period_s > kPeriodMax || c.keep_days < kKeepMin ||
        c.keep_days > kKeepMax || c.cap_mb < kCapMin || c.cap_mb > kCapMax) {
        if (why)
            std::snprintf(why, cap, "period %u..%u s, keep %u..%u days, cap %u..%u MB", kPeriodMin,
                          kPeriodMax, kKeepMin, kKeepMax, kCapMin, kCapMax);
        return Status::BadArg;
    }
    uint32_t cluster;
    {
        port::Lock lk{hmx_};
        cluster = hsnap_.cluster ? hsnap_.cluster : kAssumedCluster;
    }
    const uint64_t proj = log_projected(c, cluster);
    const uint64_t lim = uint64_t{c.cap_mb} * 1'000'000ull;
    if (proj > lim) {
        if (why)
            std::snprintf(why, cap,
                          "%u s x %u days = %.1f MB on the card (%u KB clusters) > cap %u MB",
                          c.period_s, c.keep_days, proj / 1e6,
                          static_cast<unsigned>(cluster / 1024), c.cap_mb);
        return Status::Denied;
    }
    bool period_changed;
    {
        port::Lock lk{hmx_};
        period_changed = c.period_s != cfg_.period_s;
        cfg_ = c;
        hsnap_.cfg = c;
        if (period_changed) bucket_ = 0;  // the next tick starts a period on the new grid
        prune_due_ = true;                // a shorter keep or a smaller cap applies now
    }
    (void)hal::store::set_i32(kKeyPeriod, c.period_s);
    (void)hal::store::set_i32(kKeyKeep, c.keep_days);
    (void)hal::store::set_i32(kKeyCap, c.cap_mb);
    (void)hal::store::set_i32(kKeyOn, c.on ? 1 : 0);
    uint8_t a[4];
    a[0] = static_cast<uint8_t>(c.period_s);
    a[1] = static_cast<uint8_t>(c.period_s >> 8);
    a[2] = static_cast<uint8_t>(c.keep_days);
    a[3] = static_cast<uint8_t>(c.keep_days >> 8);
    log_event(hist::Ev::LogConfig, a, sizeof a);
    CLK_LOGI(storage, "log: every %u s, keep %u days, cap %u MB, %s -- projected %.1f MB",
             c.period_s, c.keep_days, c.cap_mb, c.on ? "on" : "off", proj / 1e6);
    return Status::Ok;
}

void Storage::log_feed(transport::Snapshot const& s) noexcept {
    using namespace transport;
    port::Lock lk{hmx_};
    if (!cfg_.on) return;
    Acc& a = acc_;
    ++a.n;
    if (s.flags & kEnvOk) {
        ++a.n_env;
        a.temp += s.temp_cdeg;
        a.rh += s.rh_cpct;
        a.press += s.press_dhpa;
        a.lgas += hist::enc_gas(s.gas_ohms);  // mean of the log: a geometric mean of ohms
        a.sticky |= hist::kEnvOk;
        if (s.flags & kEnvGasValid) a.sticky |= hist::kGasValid;
        if (s.flags & kEnvHeatStable) a.sticky |= hist::kHeatStable;
    }
    if (s.flags & kAlsOk) {
        ++a.n_als;
        const float lux = s.lux < 0 ? 88000.0f : s.lux;  // -1 = saturated
        a.lux += lux;
        if (lux > a.lux_max) a.lux_max = lux;
        a.sticky |= hist::kAlsOk;
        if (s.flags & kAlsSaturated || s.lux < 0) a.sticky |= hist::kAlsSat;
    }
    if (s.flags & kPowerOk) {
        ++a.n_pow;
        a.vbat += s.vbat_mv;
        a.sticky |= hist::kPowerOk;
    }
    a.last = s;
}

void Storage::log_event(hist::Ev code, const uint8_t* args, std::size_t n) noexcept {
    port::Lock lk{hmx_};
    if (pend_n_ == kPendingMax) {
        for (std::size_t i = 1; i < pend_n_; ++i) pend_[i - 1] = pend_[i];  // keep the newest
        --pend_n_;
    }
    Pending& p = pend_[pend_n_++];
    p.mono_us = port::now_us();
    p.e = hist::Event{};
    p.e.code = code;
    if (args && n) std::memcpy(p.e.args, args, n < sizeof p.e.args ? n : sizeof p.e.args);
}

Storage::LogSnap Storage::log_snapshot() const noexcept {
    port::Lock lk{hmx_};
    LogSnap s = hsnap_;
    s.cfg = cfg_;
    s.ram = static_cast<uint32_t>(ring_n_);
    s.ram_cap = static_cast<uint32_t>(ring_cap_);
    s.flush_ago_s = s.flushes
                        ? static_cast<uint32_t>((port::now_us() - last_flush_us_) / 1'000'000ull)
                        : UINT32_MAX;
    return s;
}

std::size_t Storage::log_tail(uint8_t (*out)[hist::kRecord], std::size_t max) const noexcept {
    port::Lock lk{hmx_};
    const std::size_t n = std::min(max, ring_n_);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t k = (ring_head_ + ring_n_ - n + i) % ring_cap_;
        std::memcpy(out[i], ring_[k], hist::kRecord);
    }
    return n;
}

// ---- this AO ---------------------------------------------------------------------------------

void Storage::log_start() noexcept {
    LogCfg c{};
    if (const auto v = hal::store::get_i32(kKeyPeriod);
        v.ok() && v.v >= kPeriodMin && v.v <= kPeriodMax)
        c.period_s = static_cast<uint16_t>(v.v);
    if (const auto v = hal::store::get_i32(kKeyKeep); v.ok() && v.v >= kKeepMin && v.v <= kKeepMax)
        c.keep_days = static_cast<uint16_t>(v.v);
    if (const auto v = hal::store::get_i32(kKeyCap); v.ok() && v.v >= kCapMin && v.v <= kCapMax)
        c.cap_mb = static_cast<uint16_t>(v.v);
    if (const auto v = hal::store::get_i32(kKeyOn); v.ok()) c.on = v.v != 0;
    auto* ring = new (std::nothrow) uint8_t[kRingRecords][hist::kRecord];
    {
        port::Lock lk{hmx_};
        cfg_ = c;
        hsnap_.cfg = c;
        ring_ = ring;
        ring_cap_ = ring ? kRingRecords : 0;
        hsnap_.cluster = mounted_ ? cluster_of(card_) : kAssumedCluster;
    }
    flush_at_us_ = port::now_us() + kFlushEveryUs;
    if (!ring) CLK_LOGE(storage, "log: no memory for the RAM ring -- history is off");
    const auto si = hal::sys::info();
    uint8_t a[5] = {si.reset_reason, static_cast<uint8_t>(fw_id_),
                    static_cast<uint8_t>(fw_id_ >> 8), static_cast<uint8_t>(fw_id_ >> 16),
                    static_cast<uint8_t>(fw_id_ >> 24)};
    log_event(hist::Ev::Boot, a, sizeof a);
    CLK_LOGI(storage, "log: every %u s, keep %u days, cap %u MB%s", c.period_s, c.keep_days,
             c.cap_mb, c.on ? "" : " -- OFF");
}

void Storage::log_push(const uint8_t* rec) noexcept {
    if (!ring_) return;
    if (ring_n_ == ring_cap_) {  // full, no card: the oldest goes
        ring_head_ = (ring_head_ + 1) % ring_cap_;
        --ring_n_;
        ++hsnap_.dropped;
    }
    std::memcpy(ring_[(ring_head_ + ring_n_) % ring_cap_], rec, hist::kRecord);
    ++ring_n_;
}

// The period that began at `t` is over: its average becomes a record.  hmx_ held.
void Storage::log_emit(uint32_t t) noexcept {
    Acc& a = acc_;
    if (a.n == 0) return;
    hist::Sample s{};
    s.t = t;
    s.n = static_cast<uint8_t>(a.n > 255 ? 255 : a.n);
    if (a.n_env) {
        s.temp_cdeg = static_cast<int16_t>(a.temp / static_cast<int64_t>(a.n_env));
        s.rh_cpct = static_cast<uint16_t>(a.rh / static_cast<int64_t>(a.n_env));
        s.press_dhpa = static_cast<uint16_t>(a.press / static_cast<int64_t>(a.n_env));
        s.gas_ohms = hist::dec_gas(static_cast<uint16_t>(a.lgas / a.n_env + 0.5));
    }
    if (a.n_als) {
        s.lux = static_cast<float>(a.lux / a.n_als);
        s.lux_max = a.lux_max;
    }
    if (a.n_pow) s.vbat_mv = static_cast<uint16_t>(a.vbat / a.n_pow);
    const auto& l = a.last;
    using namespace transport;
    uint16_t f = a.sticky;
    if (a.n_pow) {
        s.soc_pct = l.soc_pct;
        if (l.flags & kPlugged) f |= hist::kPlugged;
        if (l.flags & kCharging) f |= hist::kCharging;
    }
    if (l.flags & kBattLow) f |= hist::kBattLow;
    if (l.flags & kRadioOff) f |= hist::kRadioOff;
    if (l.flags & kAlarmArmed) f |= hist::kAlarmArmed;
    if (l.flags & kNetSynced) f |= hist::kTimeSntp;
    if (l.wifi_state == static_cast<uint8_t>(WifiState::Online)) {
        f |= hist::kWifiOnline;
        s.rssi = l.wifi_rssi;
    }
    s.flags = f;
    uint8_t rec[hist::kRecord];
    hist::encode(s, rec);
    log_push(rec);
    a = Acc{};
}

void Storage::log_tick() noexcept {
    const auto c = chrono().snapshot();
    const bool time_ok = c.valid && c.date_valid && c.epoch_ms > 0;
    const uint32_t now = time_ok ? static_cast<uint32_t>(c.epoch_ms / 1000) : 0;
    std::size_t ram;
    {
        port::Lock lk{hmx_};
        hsnap_.time_ok = time_ok;
        if (time_ok) {
            // Held events, back-dated to when they happened.
            const uint64_t mono = port::now_us();
            for (std::size_t i = 0; i < pend_n_; ++i) {
                const uint64_t ago_s = (mono - pend_[i].mono_us) / 1'000'000ull;
                pend_[i].e.t = now > ago_s ? now - static_cast<uint32_t>(ago_s) : now;
                uint8_t rec[hist::kRecord];
                hist::encode(pend_[i].e, rec);
                log_push(rec);
            }
            pend_n_ = 0;
            if (cfg_.on) {
                const uint32_t bucket = now - now % cfg_.period_s;
                if (bucket_ == 0) {
                    bucket_ = bucket;
                } else if (bucket != bucket_) {
                    log_emit(bucket_);
                    bucket_ = bucket;
                }
            }
        }
        ram = ring_n_;
    }
    if (fetch_fd_ >= 0) log_fetch_pump();
    const uint64_t t = port::now_us();
    if (ram && playing_ == Playing::Nothing && fetch_fd_ < 0 &&
        (t >= flush_at_us_ || ram >= kFlushAtRecords)) {
        (void)log_write();
    }
}

// RAM -> card.  Records go to the file of their own UTC day, in order; a record the card
// refuses stays in RAM for the next try.
Status Storage::log_write() noexcept {
    flush_at_us_ = port::now_us() + kFlushEveryUs;
    if (ensure_mounted() != Status::Ok) {
        port::Lock lk{hmx_};
        hsnap_.last_err = "no card";
        return Status::NotPresent;
    }
    uint32_t cluster = cluster_of(card_);
    uint32_t newest_day = 0;
    int fd = -1;
    uint32_t fd_day = 0;
    std::size_t done = 0;
    const char* err = nullptr;
    for (;;) {
        uint8_t rec[hist::kRecord];
        uint16_t period;
        {
            port::Lock lk{hmx_};
            if (done == ring_n_) break;
            std::memcpy(rec, ring_[(ring_head_ + done) % ring_cap_], hist::kRecord);
            period = cfg_.period_s;
        }
        const uint32_t t = static_cast<uint32_t>(rec[2]) | static_cast<uint32_t>(rec[3]) << 8 |
                           static_cast<uint32_t>(rec[4]) << 16 |
                           static_cast<uint32_t>(rec[5]) << 24;
        const uint32_t day = hist::yyyymmdd(t);
        if (fd < 0 || day != fd_day) {
            if (fd >= 0) hal::sd::close(fd);
            fd = -1;
            char path[40];
            (void)hal::sd::mkdir(kLogDir);
            std::snprintf(path, sizeof path, "%s/%04u", kLogDir,
                          static_cast<unsigned>(day / 10000));
            (void)hal::sd::mkdir(path);
            day_file_path(day, path, sizeof path);
            const auto h = hal::sd::create(path, true);
            if (!h.ok()) {
                err = "could not open the day file";
                break;
            }
            fd = h.v;
            fd_day = day;
            const auto sz = hal::sd::size(fd);
            const uint32_t size = sz.ok() ? sz.v : 0;
            if (size == 0) {
                hist::Header hd{period, hist::day_start(t), fw_id_};
                uint8_t hb[hist::kHeader];
                hist::encode(hd, hb);
                if (hal::sd::write(fd, hb, sizeof hb).v != sizeof hb) {
                    err = "card write failed";
                    break;
                }
            } else if (size < hist::kHeader || (size - hist::kHeader) % hist::kRecord) {
                // A torn tail from a power cut.  Pad it to a whole slot: that slot fails its
                // CRC and is skipped, and every record after it is aligned again.
                const uint32_t rem = size < hist::kHeader
                                         ? hist::kHeader - size
                                         : hist::kRecord - (size - hist::kHeader) % hist::kRecord;
                static const uint8_t zero[hist::kHeader] = {};
                (void)hal::sd::write(fd, zero, rem);
            }
        }
        if (hal::sd::write(fd, rec, sizeof rec).v != sizeof rec) {
            err = "card write failed";
            break;
        }
        ++done;
        if (day > newest_day) newest_day = day;
    }
    if (fd >= 0) hal::sd::close(fd);
    {
        port::Lock lk{hmx_};
        ring_head_ = (ring_head_ + done) % (ring_cap_ ? ring_cap_ : 1);
        ring_n_ -= done;
        hsnap_.written += static_cast<uint32_t>(done);
        ++hsnap_.flushes;
        last_flush_us_ = port::now_us();
        hsnap_.last_err = err;
        hsnap_.cluster = cluster;
    }
    if (err) {
        CLK_LOGW(storage, "log: %s after %zu record(s)", err, done);
        mounted_ = hal::sd::mounted();
        return Status::Failed;
    }
    if (done) CLK_LOGD(storage, "log: %zu record(s) to the card", done);
    bool due;
    {
        port::Lock lk{hmx_};
        due = prune_due_;
        prune_due_ = false;
    }
    if (newest_day && (due || newest_day != pruned_day_)) log_prune(newest_day);
    return Status::Ok;
}

// Retention: whole days, oldest first -- by age, then by bytes on the card.  Never today.
void Storage::log_prune(uint32_t today) noexcept {
    pruned_day_ = today;
    const std::size_t cap = kKeepMax + 64;
    auto* days = new (std::nothrow) DayFile[cap];
    if (!days) return;
    const std::size_t n = list_days(days, cap);
    LogCfg c;
    uint32_t cluster;
    {
        port::Lock lk{hmx_};
        c = cfg_;
        cluster = hsnap_.cluster ? hsnap_.cluster : kAssumedCluster;
    }
    const uint32_t today_t = hist::from_yyyymmdd(today);
    const uint32_t oldest_kept = today_t > (c.keep_days - 1u) * 86400u
                                     ? hist::yyyymmdd(today_t - (c.keep_days - 1u) * 86400u)
                                     : 0;
    auto rounded = [cluster](uint32_t sz) {
        return (uint64_t{sz} + cluster - 1) / cluster * cluster;
    };
    uint64_t used = 0;
    for (std::size_t i = 0; i < n; ++i) used += rounded(days[i].size);
    const uint64_t lim = uint64_t{c.cap_mb} * 1'000'000ull;
    std::size_t first = 0, removed = 0;
    for (; first < n && days[first].day != today; ++first) {
        if (days[first].day >= oldest_kept && used <= lim) break;
        char path[40];
        day_file_path(days[first].day, path, sizeof path);
        if (hal::sd::remove(path) == Status::Ok) {
            used -= rounded(days[first].size);
            ++removed;
        }
    }
    if (removed) {
        CLK_LOGI(storage, "log: removed %zu old day file(s); %.1f MB kept", removed, used / 1e6);
        if (const auto in = hal::sd::info(); in.ok()) card_ = in.v;
    }
    {
        port::Lock lk{hmx_};
        hsnap_.used_bytes = used;
        hsnap_.days = static_cast<uint32_t>(n - removed);
        hsnap_.oldest = first < n ? days[first].day : 0;
        hsnap_.newest = n ? days[n - 1].day : 0;
    }
    delete[] days;
}

// ---- the download ----------------------------------------------------------------------------

Status Storage::log_fetch_begin(uint32_t day, uint32_t off) noexcept {
    log_fetch_end(nullptr);
    if (ensure_mounted() != Status::Ok) return Status::NotPresent;
    (void)log_write();  // so today's file has everything up to now
    char path[40];
    day_file_path(day, path, sizeof path);
    const auto fd = hal::sd::open(path);
    if (!fd.ok()) return Status::Failed;
    const auto sz = hal::sd::size(fd.v);
    if (!sz.ok() || off > sz.v || hal::sd::seek(fd.v, off) != Status::Ok) {
        hal::sd::close(fd.v);
        return sz.ok() && off > sz.v ? Status::BadArg : Status::Failed;
    }
    // CRC-32 of exactly the bytes that will be sent, so the phone can check what it got.
    uint32_t crc = 0;
    uint8_t buf[512];
    for (uint32_t left = sz.v - off; left;) {
        const auto r = hal::sd::read(fd.v, buf, left < sizeof buf ? left : sizeof buf);
        if (!r.ok() || r.v == 0) break;
        crc = crc32(crc, buf, r.v);
        left -= static_cast<uint32_t>(r.v);
    }
    (void)hal::sd::seek(fd.v, off);
    fetch_fd_ = fd.v;
    fetch_off_ = off;
    fetch_end_ = sz.v;
    fetch_pkt_n_ = 0;
    {
        port::Lock lk{hmx_};
        hsnap_.fetching = off < sz.v;
        hsnap_.fetch_day = day;
        hsnap_.fetch_from = off;
        hsnap_.fetch_size = sz.v;
        hsnap_.fetch_crc = crc;
        hsnap_.fetch_sent = 0;
    }
    if (off == sz.v) {  // nothing to send: the phone is up to date
        log_fetch_end(nullptr);
        return Status::Ok;
    }
    CLK_LOGI(storage, "log: sending %s from %" PRIu32 " (%" PRIu32 " bytes)", path, off,
             sz.v - off);
    retick();
    return Status::Ok;
}

// Up to 16 notifications per 10 ms tick.  A packet the radio has no buffer for is kept and
// offered again next tick; a phone that stopped listening ends it.
void Storage::log_fetch_pump() noexcept {
    for (int i = 0; i < 16; ++i) {
        if (fetch_pkt_n_ == 0) {
            if (fetch_off_ >= fetch_end_) return log_fetch_end(nullptr);
            const auto l = hal::ble::link();
            std::size_t room = l.mtu > 3 + 4 + 16 ? l.mtu - 3u - 4u : 16u;
            if (room > sizeof fetch_pkt_ - 4) room = sizeof fetch_pkt_ - 4;
            const uint32_t want =
                std::min<uint32_t>(static_cast<uint32_t>(room), fetch_end_ - fetch_off_);
            const auto r = hal::sd::read(fetch_fd_, fetch_pkt_ + 4, want);
            if (!r.ok() || r.v == 0) return log_fetch_end("card read failed");
            fetch_pkt_[0] = static_cast<uint8_t>(fetch_off_);
            fetch_pkt_[1] = static_cast<uint8_t>(fetch_off_ >> 8);
            fetch_pkt_[2] = static_cast<uint8_t>(fetch_off_ >> 16);
            fetch_pkt_[3] = static_cast<uint8_t>(fetch_off_ >> 24);
            fetch_pkt_n_ = 4 + r.v;
        }
        const Status st = hal::ble::notify_bulk(fetch_pkt_, fetch_pkt_n_);
        if (st == Status::Busy) return;  // no buffer: next tick
        if (st != Status::Ok) return log_fetch_end("the phone stopped listening");
        fetch_off_ += static_cast<uint32_t>(fetch_pkt_n_ - 4);
        {
            port::Lock lk{hmx_};
            hsnap_.fetch_sent += static_cast<uint32_t>(fetch_pkt_n_ - 4);
        }
        fetch_pkt_n_ = 0;
    }
}

void Storage::log_fetch_end(const char* why) noexcept {
    if (fetch_fd_ >= 0) {
        hal::sd::close(fetch_fd_);
        if (why) CLK_LOGW(storage, "log: download ended: %s", why);
    }
    fetch_fd_ = -1;
    fetch_pkt_n_ = 0;
    {
        port::Lock lk{hmx_};
        hsnap_.fetching = false;
        if (why) hsnap_.last_err = why;
    }
    retick();
}

}  // namespace clk::svc
