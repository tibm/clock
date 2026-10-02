// The `log` group: the history log on the card.       [FIRMWARE.md §6.3a, app/PROTOCOL.md
// "History"]
//
// Settings with a budget check, the list of day files, and the download.  `=` pairs are the
// app's (stable, documented in PROTOCOL.md); the `|` lines are for people.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/services/storage.hpp"
#include "clk/transport/history.hpp"
#include "sto_wait.hpp"

namespace clk::cli {
namespace {

namespace hist = transport::hist;
using svc::Storage;

bool parse_u(const char* s, unsigned long lo, unsigned long hi, unsigned long& v) {
    if (!s) return false;
    char* end = nullptr;
    v = std::strtoul(s, &end, 10);
    return end && !*end && v >= lo && v <= hi;
}

void kv_u(Sink& out, const char* k, unsigned long long v) {
    char b[24];
    std::snprintf(b, sizeof b, "%llu", v);
    out.kv(k, b);
}

Status cmd_status(Args const&, Sink& out) {
    const auto l = svc::storage().log_snapshot();
    const auto& c = l.cfg;
    out.printf("log     %s   every %u s   keep %u days   cap %u MB", c.on ? "on" : "OFF",
               c.period_s, c.keep_days, c.cap_mb);
    out.printf("budget  %.1f MB projected on the card (%u KB clusters)",
               Storage::log_projected(c, l.cluster) / 1e6, static_cast<unsigned>(l.cluster / 1024));
    out.printf("card    %" PRIu32 " day file(s), %.2f MB used, %" PRIu32 " .. %" PRIu32, l.days,
               l.used_bytes / 1e6, l.oldest, l.newest);
    char ago[24] = "never";
    if (l.flush_ago_s != UINT32_MAX)
        std::snprintf(ago, sizeof ago, "%" PRIu32 " s ago", l.flush_ago_s);
    out.printf("ram     %" PRIu32 " / %" PRIu32 " records waiting   last write %s   %" PRIu32
               " written, %" PRIu32 " dropped",
               l.ram, l.ram_cap, ago, l.written, l.dropped);
    if (!l.time_ok)
        out.line("time    not set yet -- nothing is recorded until the clock has a date");
    if (l.fetching) {
        char what[24];
        if (l.fetch_day) {
            std::snprintf(what, sizeof what, "%" PRIu32, l.fetch_day);
        } else {
            std::snprintf(what, sizeof what, "%s", l.fetch_file);
        }
        out.printf("fetch   %s: %" PRIu32 " of %" PRIu32 " bytes sent", what, l.fetch_sent,
                   l.fetch_size - l.fetch_from);
    }
    if (l.last_err) out.printf("last    %s", l.last_err);
    out.kv("on", c.on ? "1" : "0");
    kv_u(out, "period", c.period_s);
    kv_u(out, "keep", c.keep_days);
    kv_u(out, "cap", c.cap_mb);
    kv_u(out, "projected", Storage::log_projected(c, l.cluster));
    kv_u(out, "used", l.used_bytes);
    kv_u(out, "days", l.days);
    kv_u(out, "ram", l.ram);
    return Status::Ok;
}

// One setting, checked against the budget with the other two as they are.
Status set_one(Args const& a, Sink& out, const char* what, unsigned long lo, unsigned long hi,
               uint16_t Storage::LogCfg::* field) {
    auto c = svc::storage().log_cfg();
    if (a.count() == 0) {
        out.printf("%s %u", what, c.*field);
        return Status::Ok;
    }
    unsigned long v = 0;
    if (!parse_u(a.arg(0), lo, hi, v)) {
        out.printf("%s: want %lu..%lu", what, lo, hi);
        return Status::BadArg;
    }
    c.*field = static_cast<uint16_t>(v);
    char why[120];
    const Status st = svc::storage().log_set(c, why, sizeof why);
    if (st != Status::Ok) {
        out.printf("refused: %s", why);
        return st;
    }
    const auto l = svc::storage().log_snapshot();
    out.printf("%s %lu -- %.1f MB projected (cap %u MB)", what, v,
               Storage::log_projected(c, l.cluster) / 1e6, c.cap_mb);
    return Status::Ok;
}

Status cmd_period(Args const& a, Sink& out) {
    return set_one(a, out, "period_s", Storage::kPeriodMin, Storage::kPeriodMax,
                   &Storage::LogCfg::period_s);
}
Status cmd_keep(Args const& a, Sink& out) {
    return set_one(a, out, "keep_days", Storage::kKeepMin, Storage::kKeepMax,
                   &Storage::LogCfg::keep_days);
}
Status cmd_cap(Args const& a, Sink& out) {
    return set_one(a, out, "cap_mb", Storage::kCapMin, Storage::kCapMax, &Storage::LogCfg::cap_mb);
}

Status cmd_onoff(Args const& a, Sink& out) {
    const auto v = a.sv(0);
    if (v != "on" && v != "off") {
        out.line("usage: log enable <on|off>");
        return Status::BadArg;
    }
    auto c = svc::storage().log_cfg();
    c.on = v == "on";
    char why[120];
    const Status st = svc::storage().log_set(c, why, sizeof why);
    if (st != Status::Ok) {
        out.printf("refused: %s", why);
        return st;
    }
    out.printf("log %s", c.on ? "on" : "off -- nothing new is recorded; the files stay");
    return Status::Ok;
}

Status cmd_flush(Args const&, Sink& out) {
    const Status st = sto_await(svc::storage().log_flush(), "flush", out);
    if (st == Status::Ok) out.line("RAM records written to the card");
    return st;
}

struct DayList {
    Sink* out;
    uint32_t year;
    uint32_t* days;
    uint32_t* sizes;
    std::size_t n, cap;
};

bool on_day(hal::sd::Entry const& e, void* ctx) {
    auto* d = static_cast<DayList*>(ctx);
    unsigned mmdd = 0;
    char ext[8] = "";
    if (!e.dir && std::strlen(e.name) == 8 && std::sscanf(e.name, "%4u.%3s", &mmdd, ext) == 2 &&
        std::strcmp(ext, "bin") == 0 && d->n < d->cap) {
        d->days[d->n] = d->year * 10000 + mmdd;
        d->sizes[d->n] = e.size;
        ++d->n;
    }
    return true;
}

bool on_year(hal::sd::Entry const& e, void* ctx) {
    auto* y = static_cast<uint32_t*>(ctx);
    unsigned v = 0;
    if (e.dir && std::strlen(e.name) == 4 && std::sscanf(e.name, "%4u", &v) == 1 && y[0] < 63)
        y[++y[0]] = v;
    return true;
}

// `=day=<yyyymmdd>/<bytes>` per file, oldest first.  Flushes first so today is complete.
// Files only ever grow (append) or disappear (retention), so the size is all a phone needs to
// know what it is missing.
Status cmd_days(Args const&, Sink& out) {
    (void)sto_await(svc::storage().log_flush(), "flush", out);  // no card is answered below
    if (!hal::sd::mounted()) {
        out.line("no card");
        return Status::NotPresent;
    }
    uint32_t years[64] = {};
    if (hal::sd::list("/sd/log", &on_year, years) != Status::Ok) {
        out.line("0 day files");
        return Status::Ok;
    }
    constexpr std::size_t kCap = Storage::kKeepMax + 64;
    auto* days = new (std::nothrow) uint32_t[kCap];
    auto* sizes = new (std::nothrow) uint32_t[kCap];
    if (!days || !sizes) {
        delete[] days;
        delete[] sizes;
        out.line("out of memory");
        return Status::Failed;
    }
    DayList d{&out, 0, days, sizes, 0, kCap};
    for (uint32_t i = 1; i <= years[0]; ++i) {
        char dir[24];
        std::snprintf(dir, sizeof dir, "/sd/log/%04u", static_cast<unsigned>(years[i]));
        d.year = years[i];
        (void)hal::sd::list(dir, &on_day, &d);
    }
    // Insertion sort: a few hundred entries, mostly in order already.
    for (std::size_t i = 1; i < d.n; ++i) {
        for (std::size_t j = i; j > 0 && days[j - 1] > days[j]; --j) {
            std::swap(days[j - 1], days[j]);
            std::swap(sizes[j - 1], sizes[j]);
        }
    }
    uint64_t total = 0;
    for (std::size_t i = 0; i < d.n; ++i) {
        char v[32];
        std::snprintf(v, sizeof v, "%" PRIu32 "/%" PRIu32, days[i], sizes[i]);
        out.kv("day", v);
        total += sizes[i];
    }
    out.printf("%zu day file(s), %.2f MB", d.n, total / 1e6);
    delete[] days;
    delete[] sizes;
    return Status::Ok;
}

// `log fetch <yyyymmdd> [<offset>]` -- the bytes go out on `bulk`.
Status cmd_fetch(Args const& a, Sink& out) {
    if (a.sv(0) == "stop") {
        return sto_await(svc::storage().log_fetch_stop(), "fetch stop", out);
    }
    unsigned long day = 0, off = 0;
    if (!parse_u(a.arg(0), 19700101, 21051231, day) || !hist::from_yyyymmdd(day) ||
        (a.count() > 1 && !parse_u(a.arg(1), 0, 0xFFFFFFFFul, off))) {
        out.line("usage: log fetch <yyyymmdd> [<offset>]   |   log fetch stop");
        return Status::BadArg;
    }
    const auto l0 = hal::ble::link();
    const Status st =
        sto_await(svc::storage().log_fetch(static_cast<uint32_t>(day), static_cast<uint32_t>(off)),
                  "fetch", out);
    if (st != Status::Ok) return st;
    const auto l = svc::storage().log_snapshot();
    char v[16];
    std::snprintf(v, sizeof v, "%lu", day);
    out.kv("day", v);
    kv_u(out, "from", l.fetch_from);
    kv_u(out, "size", l.fetch_size);
    std::snprintf(v, sizeof v, "%08" PRIx32, l.fetch_crc);
    out.kv("crc", v);
    out.printf("sending %" PRIu32 " bytes on `bulk`%s", l.fetch_size - l.fetch_from,
               l0.bulk_sub ? "" : " -- nobody is subscribed to it, so nothing will arrive");
    return Status::Ok;
}

// The newest records still in RAM, decoded -- the bench view of what is being recorded.
Status cmd_tail(Args const& a, Sink& out) {
    unsigned long n = 10;
    if (a.count() && !parse_u(a.arg(0), 1, 100, n)) {
        out.line("usage: log tail [<1..100>]");
        return Status::BadArg;
    }
    uint8_t recs[100][hist::kRecord];
    const std::size_t got = svc::storage().log_tail(recs, n);
    if (got == 0) out.line("no records in RAM (`log flush` writes them to the card)");
    for (std::size_t i = 0; i < got; ++i) {
        hist::Record r;
        if (!hist::decode(recs[i], r)) {
            out.line("  (bad CRC)");
            continue;
        }
        const std::time_t t = r.kind == hist::kSample ? r.s.t : r.e.t;
        std::tm tm{};
        gmtime_r(&t, &tm);
        char when[24];
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%SZ", &tm);
        if (r.kind == hist::kSample) {
            const auto& s = r.s;
            out.printf("  %s  n=%u  %.2f C  %.2f %%RH  %.1f hPa  gas %" PRIu32
                       "  lux %.1f (max %.0f)  %u mV  soc %u  rssi %d  flags %04x",
                       when, s.n, s.temp_cdeg / 100.0, s.rh_cpct / 100.0, s.press_dhpa / 10.0,
                       s.gas_ohms, static_cast<double>(s.lux), static_cast<double>(s.lux_max),
                       s.vbat_mv, s.soc_pct, s.rssi, s.flags);
        } else {
            out.printf("  %s  event %s  %02x %02x %02x %02x %02x", when, hist::name(r.e.code),
                       r.e.args[0], r.e.args[1], r.e.args[2], r.e.args[3], r.e.args[4]);
        }
    }
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"log", nullptr, "status", "", "history log: settings, budget, card, RAM", ReleaseOk,
     cmd_status},
    {"log", nullptr, "period", "[<s>]", "one record per this many seconds (10..3600)", ReleaseOk,
     cmd_period},
    {"log", nullptr, "keep", "[<days>]", "days kept on the card (1..3650)", ReleaseOk, cmd_keep},
    {"log", nullptr, "cap", "[<MB>]", "most the log may take on the card (10..2000)", ReleaseOk,
     cmd_cap},
    {"log", nullptr, "enable", "<on|off>", "record, or stop recording", ReleaseOk, cmd_onoff},
    {"log", nullptr, "flush", "", "write the RAM records to the card now", ReleaseOk, cmd_flush},
    {"log", nullptr, "days", "", "day files on the card (`=day=` pairs)", ReleaseOk, cmd_days},
    {"log", nullptr, "fetch", "<yyyymmdd> [<off>] | stop", "send a day file on `bulk`", ReleaseOk,
     cmd_fetch},
    {"log", nullptr, "tail", "[<n>]", "the newest records in RAM, decoded", ReleaseOk, cmd_tail},
};

}  // namespace

extern const CmdTable kTableLog{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
