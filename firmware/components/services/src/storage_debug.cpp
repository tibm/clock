// The debug journal on the card: one text file per boot.        [FIRMWARE.md §9.4a]
//
// clk::journal keeps every log line in a 16 KB RAM ring; this drains it to
// `/sd/debug/<boot>.log` every kDbgEveryMs.  The file is opened, appended and closed each
// time, so a power cut costs at most the last few seconds and never the file -- and those
// seconds are still in `.noinit` RAM after anything short of a power cut.
//
// What the ring carried over a reset belongs to the PREVIOUS boot and goes to the end of its
// file, under a marker.  That is the useful part when the clock hangs: the task watchdog
// resets it ~10 s later, and the lines that say what it was doing are in RAM, not on the card.
//
// A file only ever GROWS.  Past kDbgFileCap a boot starts its next part, `<boot>-<part>.log`,
// instead of renaming anything -- so the phone can mirror the directory by name and size
// (`sys journal fetch`), and no file is renamed under an open download.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "clk/journal.hpp"
#include "clk/log.hpp"
#include "clk/services/storage.hpp"

namespace clk::svc {
namespace {

constexpr const char* kKeyBoot = "sys.boot";  // NVS: the boot counter, one per app_main
constexpr uint64_t kMountRetryUs = 60'000'000;
constexpr int kMaxChunks = 8;  // per flush: 8 x 8 KB, several times the whole ring

struct DbgList {
    Storage::DbgFile* f;
    std::size_t max;
    std::size_t n = 0;
    std::size_t total = 0;
    uint64_t bytes = 0;
};

bool collect(hal::sd::Entry const& e, void* ctx) {
    auto& l = *static_cast<DbgList*>(ctx);
    Storage::DbgFile d{};
    if (e.dir || !Storage::dbg_parse(e.name, d.boot, d.order)) return true;
    d.size = e.size;
    l.bytes += e.size;
    ++l.total;
    if (l.n < l.max) l.f[l.n++] = d;
    return true;
}

struct LastOrder {
    uint32_t boot;
    uint32_t order = 0;
};

bool find_last(hal::sd::Entry const& e, void* ctx) {
    auto& l = *static_cast<LastOrder*>(ctx);
    uint32_t boot = 0, order = 0;
    if (!e.dir && Storage::dbg_parse(e.name, boot, order) && boot == l.boot && order > l.order)
        l.order = order;
    return true;
}

const char* reset_name(uint8_t r) {
    static constexpr const char* kNames[] = {
        "unknown",  "power-on", "ext pin",      "sw",         "panic", "int wdt",
        "task wdt", "wdt",      "deep sleep",   "brown-out",  "sdio",  "usb",
        "jtag",     "efuse",    "power glitch", "cpu lockup",
    };
    return r < std::size(kNames) ? kNames[r] : "?";
}

}  // namespace

uint32_t Storage::dbg_flush() noexcept { return enqueue(Kind::DbgFlush, nullptr, false); }

// "000123.log" -> 123/1, "000123-4.log" -> 123/5, "000123.old" -> 123/0.  False for anything
// that is not ours.
bool Storage::dbg_parse(const char* n, uint32_t& boot, uint32_t& order) noexcept {
    boot = 0;
    for (int i = 0; i < 6; ++i) {
        if (n[i] < '0' || n[i] > '9') return false;
        boot = boot * 10 + static_cast<uint32_t>(n[i] - '0');
    }
    const char* t = n + 6;
    if (std::strcmp(t, ".log") == 0) {
        order = 1;
        return true;
    }
    if (std::strcmp(t, ".old") == 0) {
        order = 0;
        return true;
    }
    if (*t != '-' || t[1] < '1' || t[1] > '9') return false;  // no "-0", no leading zero
    uint32_t part = 0;
    int digits = 0;
    for (++t; *t >= '0' && *t <= '9'; ++t) {
        if (++digits > 6) return false;
        part = part * 10 + static_cast<uint32_t>(*t - '0');
    }
    if (std::strcmp(t, ".log") != 0) return false;
    order = part + 1;
    return true;
}

void Storage::dbg_name(uint32_t boot, uint32_t order, char* out, std::size_t cap) noexcept {
    const auto b = static_cast<unsigned>(boot % 1'000'000u);
    if (order == 0) {
        std::snprintf(out, cap, "%06u.old", b);
    } else if (order == 1) {
        std::snprintf(out, cap, "%06u.log", b);
    } else {
        std::snprintf(out, cap, "%06u-%u.log", b, static_cast<unsigned>(order - 1));
    }
}

Status Storage::dbg_list(DbgFile* out, std::size_t max, std::size_t& total,
                         uint64_t& bytes) noexcept {
    DbgList l{out, max};
    const Status st = hal::sd::list(kDbgDir, &collect, &l);
    total = l.total;
    bytes = l.bytes;
    if (st != Status::Ok) return st;
    std::qsort(out, l.n, sizeof out[0], [](const void* a, const void* b) {
        const auto& x = *static_cast<const DbgFile*>(a);
        const auto& y = *static_cast<const DbgFile*>(b);
        if (x.boot != y.boot) return x.boot < y.boot ? -1 : 1;
        return x.order == y.order ? 0 : (x.order < y.order ? -1 : 1);
    });
    return Status::Ok;
}

Storage::DbgSnap Storage::dbg_snapshot() const noexcept {
    port::Lock lk{dmx_};
    DbgSnap s = dsnap_;
    s.flush_ago_s = dbg_last_us_
                        ? static_cast<uint32_t>((port::now_us() - dbg_last_us_) / 1'000'000u)
                        : UINT32_MAX;
    return s;
}

void Storage::dbg_path(uint32_t boot, uint32_t order, char* out, std::size_t cap) noexcept {
    char name[24];
    dbg_name(boot, order, name, sizeof name);
    std::snprintf(out, cap, "%s/%s", kDbgDir, name);
}

uint32_t Storage::dbg_last_order(uint32_t boot) const noexcept {
    LastOrder l{boot};
    (void)hal::sd::list(kDbgDir, &find_last, &l);
    return l.order ? l.order : 1;
}

void Storage::dbg_start() noexcept {
    const auto prev = hal::store::get_i32(kKeyBoot);
    dbg_boot_ = prev.ok() && prev.v > 0 ? static_cast<uint32_t>(prev.v) + 1u : 1u;
    if (dbg_boot_ >= 1'000'000u) dbg_boot_ = 1;  // six digits in the name
    dbg_order_ = 1;
    dbg_prev_order_ = 0;
    dbg_size_ = 0;
    dbg_opened_ = false;
    dbg_prev_marked_ = false;
    (void)hal::store::set_i32(kKeyBoot, static_cast<int32_t>(dbg_boot_));
    {
        port::Lock lk{dmx_};
        dsnap_.boot = dbg_boot_;
    }
    const auto js = journal::stats();
    CLK_LOGI(storage, "debug journal: boot %u, %s start, %u byte(s) carried from boot %u",
             static_cast<unsigned>(dbg_boot_), js.warm ? "warm" : "cold",
             static_cast<unsigned>(js.carried), static_cast<unsigned>(dbg_boot_ - 1));
    dbg_at_us_ = port::now_us();  // first flush on the first tick: the boot lines, and the rescue
}

void Storage::dbg_tick() noexcept {
    if (dbg_prune_due_ && fetch_fd_ < 0) dbg_prune();
    if (!dbg_boot_ || port::now_us() < dbg_at_us_) return;
    (void)dbg_write();
}

Status Storage::dbg_append(int& fd, bool& fd_prev, bool prev, const char* data,
                           std::size_t n) noexcept {
    if (fd >= 0 && fd_prev != prev) {
        hal::sd::close(fd);
        fd = -1;
    }
    if (fd < 0) {
        (void)hal::sd::mkdir(kDbgDir);
        // The previous boot's lines go to the end of its newest file, whichever part that is.
        if (prev && !dbg_prev_order_) dbg_prev_order_ = dbg_last_order(dbg_boot_ - 1);
        char path[40];
        dbg_path(prev ? dbg_boot_ - 1 : dbg_boot_, prev ? dbg_prev_order_ : dbg_order_, path,
                 sizeof path);
        const auto h = hal::sd::create(path, true);
        if (!h.ok()) return Status::Failed;
        fd = h.v;
        fd_prev = prev;
        char line[200];
        int k = 0;
        if (prev && !dbg_prev_marked_) {
            k = std::snprintf(line, sizeof line,
                              "--- the last lines before the reset: kept in RAM, written by "
                              "boot %u ---\n",
                              static_cast<unsigned>(dbg_boot_));
            dbg_prev_marked_ = true;
        } else if (!prev) {
            const auto sz = hal::sd::size(fd);
            const uint32_t size = sz.ok() ? sz.v : 0;
            // Once per file: this boot's first flush, after a roll, or on a card that was
            // swapped under us (an empty file where we had written one).
            if (dbg_opened_ && size) {
                dbg_size_ = size;
            } else {
                dbg_size_ = size;
                const auto js = journal::stats();
                char part[24] = "";
                if (dbg_order_ > 1)
                    std::snprintf(part, sizeof part, " part %u",
                                  static_cast<unsigned>(dbg_order_ - 1));
                k = std::snprintf(
                    line, sizeof line,
                    "=== boot %u%s%s  reset: %s  journal: %s, %u byte(s) of boot %u "
                    "rescued, %u lost ===\n",
                    static_cast<unsigned>(dbg_boot_), part, dbg_size_ ? " (continued)" : "",
                    reset_name(hal::sys::info().reset_reason), js.warm ? "warm" : "cold",
                    static_cast<unsigned>(js.carried), static_cast<unsigned>(dbg_boot_ - 1),
                    static_cast<unsigned>(js.lost));
                dbg_opened_ = true;
                port::Lock lk{dmx_};
                std::snprintf(dsnap_.file, sizeof dsnap_.file, "%s", path);
            }
        }
        if (k > 0) {
            const auto w = hal::sd::write(fd, line, static_cast<std::size_t>(k));
            if (!w.ok() || w.v != static_cast<std::size_t>(k)) return Status::Failed;
            if (!prev) dbg_size_ += static_cast<uint32_t>(k);
        }
    }
    const auto w = hal::sd::write(fd, data, n);
    if (!w.ok() || w.v != n) return Status::Failed;
    if (!prev) dbg_size_ += static_cast<uint32_t>(n);
    return Status::Ok;
}

Status Storage::dbg_write() noexcept {
    const uint64_t now = port::now_us();
    dbg_at_us_ = now + kDbgEveryMs * 1000ull;
    if (!journal::stats().used) return Status::Ok;
    // No card: the ring keeps the newest 16 KB meanwhile.  Mounting is not free, so a card
    // that is not there is asked about once a minute, not every flush.
    if (!mounted_ || !hal::sd::mounted()) {
        if (now < dbg_mount_at_us_) return Status::NotPresent;
        dbg_mount_at_us_ = now + kMountRetryUs;
        if (ensure_mounted() != Status::Ok) {
            constexpr const char* kNoCard = "no card";
            if (dbg_err_ != kNoCard) CLK_LOGW(storage, "debug journal: no card");
            dbg_err_ = kNoCard;
            port::Lock lk{dmx_};
            dsnap_.last_err = dbg_err_;
            return Status::NotPresent;
        }
    }
    // buf_ is the audio read buffer: scratch between two pump() calls, and this is the same
    // thread, so it is free here.
    char* const buf = reinterpret_cast<char*>(buf_);
    int fd = -1;
    bool fd_prev = false;
    const bool was_opened = dbg_opened_;
    uint32_t wrote = 0, rescued = 0;
    const char* err = nullptr;
    for (int i = 0; i < kMaxChunks; ++i) {
        const auto c = journal::peek(buf, sizeof buf_);
        if (!c.n) break;
        if (dbg_append(fd, fd_prev, c.prev, buf, c.n) != Status::Ok) {
            err = "card write failed";
            break;
        }
        journal::consume(c);
        wrote += static_cast<uint32_t>(c.n);
        if (c.prev) rescued += static_cast<uint32_t>(c.n);
    }
    if (fd >= 0) hal::sd::close(fd);
    {
        port::Lock lk{dmx_};
        dsnap_.flushed += wrote;
        dsnap_.recovered += rescued;
        dsnap_.file_bytes = dbg_size_;
        dsnap_.last_err = err;
        if (!err) dbg_last_us_ = port::now_us();
    }
    if (err) {
        // The card went away or is full.  Nothing was consumed past what it took; the rest
        // waits in RAM.  Said once, not every 5 s -- and it is itself a journal line.
        if (dbg_err_ != err) CLK_LOGW(storage, "debug journal: %s", err);
        dbg_err_ = err;
        mounted_ = hal::sd::mounted();
        return Status::Failed;
    }
    dbg_err_ = nullptr;
    if (dbg_size_ >= kDbgFileCap) dbg_roll();
    if (!was_opened && dbg_opened_) dbg_prune();
    return Status::Ok;
}

// A boot that runs for weeks must not grow one file forever: at the cap it carries on in its
// next part.  Nothing is renamed -- a part is never written again once the next one starts.
void Storage::dbg_roll() noexcept {
    ++dbg_order_;
    dbg_size_ = 0;
    dbg_opened_ = false;  // the next flush writes a fresh header
    CLK_LOGI(storage, "debug journal: boot %u continues in part %u",
             static_cast<unsigned>(dbg_boot_), static_cast<unsigned>(dbg_order_ - 1));
    dbg_prune();
}

// Oldest files first, never the one being written, until the directory is inside both limits.
// A long boot's older parts go too, once they are the oldest.
void Storage::dbg_prune() noexcept {
    // FATFS must not delete a file that is open: wait for the download to end.
    dbg_prune_due_ = fetch_fd_ >= 0;
    if (dbg_prune_due_) return;
    static constexpr std::size_t kMax = 2 * kDbgKeepBoots + 32;
    static DbgFile f[kMax];  // ~2 KB: this AO's thread only, and not on its stack
    for (int pass = 0; pass < 4; ++pass) {
        std::size_t files = 0;
        uint64_t bytes = 0;
        if (dbg_list(f, kMax, files, bytes) != Status::Ok) return;
        const std::size_t n = files < kMax ? files : kMax;
        std::size_t boots = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (i == 0 || f[i].boot != f[i - 1].boot) ++boots;
        const bool skipped = files > n;
        std::size_t removed = 0;
        for (std::size_t i = 0;
             i < n && (bytes > kDbgDirCap || boots > kDbgKeepBoots || files > 2 * kDbgKeepBoots);
             ++i) {
            if (f[i].boot == dbg_boot_ && f[i].order >= dbg_order_) continue;  // being written
            char path[40];
            dbg_path(f[i].boot, f[i].order, path, sizeof path);
            if (hal::sd::remove(path) != Status::Ok) continue;
            bytes -= f[i].size;
            --files;
            ++removed;
            // The last file of a boot gone: one boot fewer (never this one -- its file stays).
            if (i + 1 == n || f[i + 1].boot != f[i].boot) --boots;
        }
        {
            port::Lock lk{dmx_};
            dsnap_.files = static_cast<uint32_t>(files);
            dsnap_.dir_bytes = bytes;
        }
        if (removed) CLK_LOGI(storage, "debug journal: pruned %zu old file(s)", removed);
        if (!skipped || !removed) return;
    }
}

}  // namespace clk::svc
