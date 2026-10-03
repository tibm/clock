// `help`, `unsafe`, and the `sys` group.                    [FIRMWARE.md §9.3, §9.4]
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "clk/cli/registry.hpp"
#include "clk/evtrace.hpp"
#include "clk/hal/hal.hpp"
#include "clk/journal.hpp"
#include "clk/log.hpp"
#include "clk/port.hpp"
#include "clk/services/storage.hpp"
#include "clk/services/supervisor.hpp"
#include "sto_wait.hpp"

namespace clk::cli {

Status cmd_sys_snap(Args const&, Sink&);  // cmd_net.cpp -- it is the record `net` serves

namespace {

using log::Level;
using log::Mod;

// ---- build identity -------------------------------------------------------------------
#ifndef CLK_APP_VERSION
#define CLK_APP_VERSION "0.1.0"
#endif
#ifndef CLK_GIT_SHA
#define CLK_GIT_SHA "unknown"
#endif
#ifndef CLK_BUILD_UTC
#define CLK_BUILD_UTC __DATE__ " " __TIME__
#endif
#ifndef CLK_PROFILE
#define CLK_PROFILE "dev"
#endif
#ifndef CLK_BOARD
#define CLK_BOARD "host"
#endif
#ifndef CLK_SDK
#define CLK_SDK "host"
#endif

constexpr BuildInfo kBuild{
    CLK_APP_VERSION, CLK_GIT_SHA, CLK_BUILD_UTC, CLK_PROFILE, CLK_BOARD, CLK_SDK,
};

// ---- help / unsafe --------------------------------------------------------------------

Status cmd_help(Args const& a, Sink& out) {
    help(out, a.arg(0), a.arg(1));
    return Status::Ok;
}

Status cmd_unsafe(Args const& a, Sink& out) {
    const auto v = a.sv(0);
    if (v.empty()) {
        out.printf("unsafe=%s", unsafe_active() ? "ON" : "OFF");
        return Status::Ok;
    }
    if (v == "on") {
        unsafe_set(true);
        out.line("unsafe ON  (expires 60 s after the last use)");
        return Status::Ok;
    }
    if (v == "off") {
        unsafe_set(false);
        out.line("unsafe OFF");
        return Status::Ok;
    }
    out.line("usage: unsafe <on|off>");
    return Status::BadArg;
}

// ---- sys ------------------------------------------------------------------------------

Status cmd_ver(Args const&, Sink& out) {
    out.printf("app     clock %s  %s  %s", kBuild.app_version, kBuild.git_sha, kBuild.build_utc);
    out.printf("build   PROFILE=%s  BOARD=%s  C++%ld", kBuild.profile, kBuild.board,
               (__cplusplus / 100L) % 100L);
    out.printf("sdk     %s", kBuild.sdk);
    return Status::Ok;
}

// `sys debug` with no args prints the table; with args it sets levels.   [§9.4]
Status cmd_debug(Args const& a, Sink& out) {
    if (a.count() == 0) {
        out.line("module      level      module      level");
        for (std::size_t i = 0; i < log::kModCount; i += 2) {
            const auto m0 = static_cast<Mod>(i);
            char buf[80];
            if (i + 1 < log::kModCount) {
                const auto m1 = static_cast<Mod>(i + 1);
                std::snprintf(buf, sizeof buf, "%-11s %-10s %-11s %-10s", log::name(m0),
                              log::name(log::get(m0)), log::name(m1), log::name(log::get(m1)));
            } else {
                std::snprintf(buf, sizeof buf, "%-11s %-10s", log::name(m0),
                              log::name(log::get(m0)));
            }
            out.line(buf);
        }
        out.printf("ceiling %s (%s build)", log::name(log::kCeiling), kBuild.profile);
        return Status::Ok;
    }
    if (a.count() < 2) {
        out.line("usage: sys debug [<module|glob|all> <off|error|warn|info|debug|verbose>]");
        return Status::BadArg;
    }

    Level lvl{};
    if (!log::parseLevel(a.sv(1), lvl)) {
        out.printf("bad level '%s' -- want off|error|warn|info|debug|verbose (prefixes ok)",
                   a.arg(1));
        return Status::BadArg;
    }
    const int n = log::setGlob(a.sv(0), lvl);
    if (n == 0) {
        out.printf("no module matches '%s'  (`sys debug` lists them)", a.arg(0));
        return Status::BadArg;
    }
    out.printf("%d module%s -> %s", n, n == 1 ? "" : "s", log::name(lvl));
    return Status::Ok;
}

Status cmd_stat(Args const&, Sink& out) {
    // Placeholder: fills in as each AO lands (FIRMWARE.md §9.7).
    out.printf("clock %s  %s/%s  sdk=%s", kBuild.app_version, kBuild.profile, kBuild.board,
               kBuild.sdk);
    out.printf("unsafe=%s", unsafe_active() ? "ON" : "OFF");
    out.line("hands  -   (motion AO not implemented yet)");
    out.line("ui     -   (ui AO not implemented yet)");
    out.line("pwr    -   (board AO not implemented yet)");
    return Status::Ok;
}

// One restart, one meaning, both builds: esp_restart() on the board, a re-exec of the
// process under clocksim.  Everything the services believe goes away, which is the whole
// difference from `sim reset` -- that one only puts the fake HARDWARE back to power-on and
// deliberately leaves motion still believing it is homed.
Status cmd_reboot(Args const& a, Sink& out) {
    if (const char* how = a.arg(0)) {
        out.printf("`reboot %s` needs the OTA/DFU partition work (§12.1); plain reboot only", how);
        return Status::NotReady;
    }
    out.line("rebooting");
    const Status st = hal::reboot();
    // Only reached when nothing restarted us -- say which of the two it was.
    out.line(st == Status::NotPresent ? "  no reset controller here (a host build with no hook)"
                                      : "  reboot failed -- still the old image");
    return st;
}

// The debug journal (§9.4a): the RAM ring, and this boot's file on the card.
Status cmd_journal(Args const& a, Sink& out) {
    const auto v = a.sv(0);
    if (v == "flush") {
        if (const Status st = sto_await(svc::storage().dbg_flush(), "flush", out); st != Status::Ok)
            return st;
    } else if (!v.empty()) {
        out.line("usage: sys journal [flush]");
        return Status::BadArg;
    }
    const auto j = journal::stats();
    const auto d = svc::storage().dbg_snapshot();
    out.printf("ram     %" PRIu32 " / %zu bytes waiting, %" PRIu32 " written, %" PRIu32
               " lost (ring full)",
               j.used, journal::kBytes, j.written, j.lost);
    out.printf("boot    %" PRIu32 "  (%s start, %" PRIu32 " bytes carried from the last boot)",
               d.boot, j.warm ? "warm" : "cold", j.carried);
    out.printf("file    %s  %" PRIu32 " bytes", d.file[0] ? d.file : "(none yet)", d.file_bytes);
    char ago[24];
    if (d.flush_ago_s == UINT32_MAX) {
        std::snprintf(ago, sizeof ago, "never");
    } else {
        std::snprintf(ago, sizeof ago, "%" PRIu32 " s ago", d.flush_ago_s);
    }
    out.printf("card    %" PRIu32 " bytes flushed (%" PRIu32 " rescued), last %s%s%s", d.flushed,
               d.recovered, ago, d.last_err ? " -- " : "", d.last_err ? d.last_err : "");
    out.printf("dir     %s  %" PRIu32 " file(s), %" PRIu64 " KB (cap %" PRIu64 " KB, %" PRIu32
               " boots)",
               svc::Storage::kDbgDir, d.files, d.dir_bytes / 1024u,
               svc::Storage::kDbgDirCap / 1024u, svc::Storage::kDbgKeepBoots);
    // The app's (app/PROTOCOL.md "Debug journal"); the lines above are for people.
    const char* slash = std::strrchr(d.file, '/');
    char n[24];
    std::snprintf(n, sizeof n, "%" PRIu32, d.boot);
    out.kv("boot", n);
    out.kv("file", slash ? slash + 1 : d.file);
    std::snprintf(n, sizeof n, "%" PRIu32, d.file_bytes);
    out.kv("bytes", n);
    std::snprintf(n, sizeof n, "%" PRIu32, j.used);
    out.kv("ram", n);
    std::snprintf(n, sizeof n, "%" PRIu32, j.lost);
    out.kv("lost", n);
    std::snprintf(n, sizeof n, "%" PRIu32, d.files);
    out.kv("files", n);
    std::snprintf(n, sizeof n, "%" PRIu64, d.dir_bytes);
    out.kv("dir_bytes", n);
    out.kv("card", d.last_err ? d.last_err : "ok");
    return Status::Ok;
}

// `sys journal files` -- what the phone mirrors: every file, oldest first, then which one is
// still growing.  Writes the RAM ring to the card first.
Status cmd_journal_files(Args const&, Sink& out) {
    using svc::Storage;
    if (const Status st = sto_await(svc::storage().dbg_flush(), "flush", out); st != Status::Ok)
        return st;
    constexpr std::size_t kMax = 2 * Storage::kDbgKeepBoots + 32;
    auto* f = new (std::nothrow) Storage::DbgFile[kMax];
    if (!f) {
        out.line("out of memory");
        return Status::Failed;
    }
    std::size_t total = 0;
    uint64_t bytes = 0;
    if (Storage::dbg_list(f, kMax, total, bytes) != Status::Ok) {
        delete[] f;
        out.line("no card");
        return Status::NotPresent;
    }
    const std::size_t n = total < kMax ? total : kMax;
    for (std::size_t i = 0; i < n; ++i) {
        char name[24], v[40];
        Storage::dbg_name(f[i].boot, f[i].order, name, sizeof name);
        std::snprintf(v, sizeof v, "%s/%" PRIu32, name, f[i].size);
        out.kv("file", v);
    }
    delete[] f;
    const auto d = svc::storage().dbg_snapshot();
    char v[16];
    std::snprintf(v, sizeof v, "%" PRIu32, d.boot);
    out.kv("boot", v);
    const char* slash = std::strrchr(d.file, '/');
    out.kv("current", slash ? slash + 1 : d.file);
    out.printf("%zu file(s), %.2f MB%s", total, bytes / 1e6,
               total > n ? " -- the oldest are not listed until the next prune" : "");
    return Status::Ok;
}

// `sys journal fetch <name> [<offset>]` -- the bytes go out on `bulk`, as for `log fetch`.
Status cmd_journal_fetch(Args const& a, Sink& out) {
    auto& sto = svc::storage();
    if (a.sv(0) == "stop") return sto_await(sto.log_fetch_stop(), "fetch stop", out);
    uint32_t boot = 0, order = 0;
    unsigned long off = 0;
    bool off_ok = true;
    if (a.count() > 1) {
        char* end = nullptr;
        off = std::strtoul(a.arg(1), &end, 10);
        off_ok = a.arg(1)[0] >= '0' && a.arg(1)[0] <= '9' && end && !*end && off <= 0xFFFFFFFFul;
    }
    if (!a.arg(0) || !svc::Storage::dbg_parse(a.arg(0), boot, order) || !off_ok) {
        out.line("usage: sys journal fetch <name> [<offset>]   |   sys journal fetch stop");
        return Status::BadArg;
    }
    const auto l0 = hal::ble::link();
    const Status st = sto_await(sto.dbg_fetch(a.arg(0), static_cast<uint32_t>(off)), "fetch", out);
    if (st != Status::Ok) return st;
    const auto l = sto.log_snapshot();
    char v[16];
    out.kv("file", a.arg(0));
    std::snprintf(v, sizeof v, "%" PRIu32, l.fetch_from);
    out.kv("from", v);
    std::snprintf(v, sizeof v, "%" PRIu32, l.fetch_size);
    out.kv("size", v);
    std::snprintf(v, sizeof v, "%08" PRIx32, l.fetch_crc);
    out.kv("crc", v);
    out.printf("sending %" PRIu32 " bytes on `bulk`%s", l.fetch_size - l.fetch_from,
               l0.bulk_sub ? "" : " -- nobody is subscribed to it, so nothing will arrive");
    return Status::Ok;
}

// The supervisor (§6.8): stalls seen, and a heartbeat on demand.
Status cmd_wd(Args const&, Sink& out) {
    auto& sup = svc::supervisor();
    const auto s = sup.snapshot();
    out.printf("hands   %s%s%s", s.stalled ? "STALLED " : "moving", s.stalled ? "-- " : "",
               s.stalled && s.why ? s.why : "");
    if (s.stalled)
        out.printf("        for %" PRIu32 " s%s", s.stalled_s,
                   s.restart ? "; restarts if it persists" : "");
    out.printf("stalls  %" PRIu32 " hands, %" PRIu32 " AO (since boot); %" PRIu32 " heartbeat(s)",
               s.hand_stalls, s.ao_stalls, s.beats);
    sup.heartbeat();
    out.line("heartbeat logged (module `sup`)");
    return Status::Ok;
}

// The fault latch (§6.6g): what the status row is blinking, and the acknowledgement a long
// press in idle also gives.
Status cmd_fault(Args const& a, Sink& out) {
    using S = svc::Supervisor;
    auto& sup = svc::supervisor();
    const char* v = a.arg(0);
    if (v && std::strcmp(v, "ack") != 0) {
        out.line("usage: sys fault [ack]");
        return Status::BadArg;
    }
    if (v) {
        const uint8_t was = sup.ack();
        out.printf("acknowledged 0x%02x", was);
    }
    const auto s = sup.snapshot();
    for (const uint8_t bit : {S::kFaultHands, S::kFaultCharger, S::kFaultAmp}) {
        const char* px = bit == S::kFaultHands ? "clock" : bit == S::kFaultCharger ? "batt" : "vol";
        out.printf("%-8s %-6s %s%s", S::fault_name(bit), px,
                   (s.faults_active & bit) ? "ACTIVE" : "ok",
                   (s.faults_shown & bit) ? "  -- blinking" : "");
    }
    out.printf("raised  %" PRIu32 " since boot", s.fault_raises);
    return Status::Ok;
}

// ---- field diagnostics: heap, tasks, coredump, event ring (§9.6) -----------------------

// "123.4 K", "7.81 M": the heaps here are 300 K and 8 M, and both need to read at a glance.
void human(char* out, std::size_t n, uint32_t b) {
    if (b >= 1024u * 1024u) {
        std::snprintf(out, n, "%.2f M", b / (1024.0 * 1024.0));
    } else {
        std::snprintf(out, n, "%.1f K", b / 1024.0);
    }
}

Status cmd_heap(Args const&, Sink& out) {
    const auto i = hal::sys::info();
    if (i.heap_free == 0) {
        out.line("no heap figures on the host");
        return Status::NotPresent;
    }
    char f[16], m[16], l[16], d[16];
    human(f, sizeof f, i.int_free);
    human(m, sizeof m, i.int_min);
    human(l, sizeof l, i.int_largest);
    human(d, sizeof d, i.dma_largest);
    // Internal first: it is the one that runs out (2026-10-01, hal.hpp sys::Info).
    out.printf("internal  free %-9s min %-9s largest %-9s dma largest %s", f, m, l, d);
    if (i.psram_total) {
        char t[16];
        human(f, sizeof f, i.psram_free);
        human(m, sizeof m, i.psram_min);
        human(l, sizeof l, i.psram_largest);
        human(t, sizeof t, i.psram_total);
        out.printf("psram     free %-9s min %-9s largest %-9s of %s", f, m, l, t);
    } else {
        out.line("psram     none");
    }
    human(f, sizeof f, i.heap_free);
    human(m, sizeof m, i.heap_min);
    out.printf("all       free %-9s min %s", f, m);
    char v[16];
    std::snprintf(v, sizeof v, "%" PRIu32, i.int_free);
    out.kv("int_free", v);
    std::snprintf(v, sizeof v, "%" PRIu32, i.int_min);
    out.kv("int_min", v);
    std::snprintf(v, sizeof v, "%" PRIu32, i.psram_free);
    out.kv("psram_free", v);
    return Status::Ok;
}

// `sys top [<ms>]`: two scheduler snapshots `ms` apart, so the CPU column is THIS window's
// load rather than an average since boot that a stuck task hides in after a day.
Status cmd_top(Args const& a, Sink& out) {
    unsigned long ms = 1000;
    if (a.count() > 0) {
        char* end = nullptr;
        ms = std::strtoul(a.arg(0), &end, 10);
        if (!end || *end || ms < 100 || ms > 5000) {
            out.line("usage: sys top [<window ms, 100..5000>]");
            return Status::BadArg;
        }
    }
    constexpr std::size_t kMax = 48;
    using hal::sys::Task;
    auto* t0 = new (std::nothrow) Task[2 * kMax];
    if (!t0) {
        out.line("out of memory");
        return Status::Failed;
    }
    Task* t1 = t0 + kMax;
    std::size_t n0 = 0, n1 = 0;
    uint32_t tot0 = 0, tot1 = 0;
    if (const Status st = hal::sys::tasks(t0, kMax, n0, tot0); st != Status::Ok) {
        delete[] t0;
        out.line(st == Status::NotPresent ? "no scheduler stats on the host" : "task list failed");
        return st;
    }
    port::Signal{}.wait_real_ms(static_cast<uint32_t>(ms));
    (void)hal::sys::tasks(t1, kMax, n1, tot1);
    const std::size_t k0 = n0 < kMax ? n0 : kMax, k1 = n1 < kMax ? n1 : kMax;
    const uint32_t span = tot1 - tot0;  // µs of wall time: one core's worth

    struct Row {
        Task const* t;
        uint32_t used;
    };
    Row rows[kMax];
    uint32_t idle[2] = {span, span};  // a core with no IDLE task seen reads 0 % busy
    for (std::size_t k = 0; k < k1; ++k) {
        uint32_t before = 0;  // a task born inside the window: all of it is this window's
        for (std::size_t j = 0; j < k0; ++j)
            if (t0[j].id == t1[k].id) before = t0[j].runtime;
        rows[k] = {&t1[k], t1[k].runtime - before};
        if (std::strcmp(t1[k].name, "IDLE0") == 0) idle[0] = rows[k].used;
        if (std::strcmp(t1[k].name, "IDLE1") == 0) idle[1] = rows[k].used;
    }
    std::sort(rows, rows + k1, [](Row const& x, Row const& y) { return x.used > y.used; });
    const auto pct = [span](uint32_t u) { return span ? 100.0 * u / span : 0.0; };

    out.line("task              core prio   cpu%  stack free  state");
    for (std::size_t k = 0; k < k1; ++k) {
        Task const& t = *rows[k].t;
        char core[4];
        std::snprintf(core, sizeof core, "%s", t.core < 0 ? "-" : t.core == 0 ? "0" : "1");
        out.printf("%-16s  %-4s %4u  %5.1f  %10" PRIu32 "  %c", t.name, core, t.prio,
                   pct(rows[k].used), t.stack_hw, t.state);
    }
    out.printf("load      core0 %.1f %%  core1 %.1f %%  (%lu ms window, %zu tasks%s)",
               100.0 - pct(idle[0] < span ? idle[0] : span),
               100.0 - pct(idle[1] < span ? idle[1] : span), ms, n1, n1 > kMax ? ", list cut" : "");
    delete[] t0;
    return Status::Ok;
}

// Xtensa EXCCAUSE, the ones a firmware bug produces.  A panic from abort() / the task
// watchdog has no exception and the reason line says what it was instead.
const char* exc_name(uint32_t c) {
    switch (c) {
        case 0:
            return "IllegalInstruction";
        case 2:
            return "InstructionFetchError";
        case 3:
            return "LoadStoreError";
        case 6:
            return "IntegerDivideByZero";
        case 9:
            return "LoadStoreAlignment";
        case 20:
            return "InstFetchProhibited";
        case 28:
            return "LoadProhibited";
        case 29:
            return "StoreProhibited";
        default:
            return "";
    }
}

Status cmd_coredump_info(Args const&, Sink& out) {
    hal::sys::Coredump cd{};
    const Status st = hal::sys::coredump(cd);
    if (st == Status::NotPresent) {
        out.line("no coredump partition here");
        return st;
    }
    if (st != Status::Ok) {
        out.line("could not read the coredump partition");
        return st;
    }
    out.kv("coredump", cd.present ? (cd.valid ? "valid" : "corrupt") : "none");
    if (!cd.present) {
        out.line("coredump  none");
        return Status::Ok;
    }
    out.printf("coredump  %" PRIu32 " bytes, %s", cd.size,
               cd.valid ? "checksum ok" : "CORRUPT -- the summary below is unavailable");
    if (!cd.valid) return Status::Ok;
    const char* en = exc_name(cd.cause);
    out.printf("task      '%s'  pc 0x%08" PRIx32 "  cause %" PRIu32 "%s%s%s  vaddr 0x%08" PRIx32,
               cd.task, cd.pc, cd.cause, *en ? " (" : "", en, *en ? ")" : "", cd.vaddr);
    if (cd.reason[0]) out.printf("reason    %s", cd.reason);
    char bt[8 * 11 + 1] = "";
    std::size_t w = 0;
    for (uint8_t k = 0; k < cd.depth && w + 12 <= sizeof bt; ++k)
        w += static_cast<std::size_t>(
            std::snprintf(bt + w, sizeof bt - w, "%s0x%08" PRIx32, k ? " " : "", cd.bt[k]));
    out.printf("backtrace %s%s", cd.depth ? bt : "(none)", cd.bt_corrupted ? "  |<-CORRUPTED" : "");
    out.printf("elf sha   %s  (`sys ver` names this image; decode against the build that matches)",
               cd.elf_sha);
    out.line("decode    idf.py -p <port> coredump-info   |   `sys coredump erase` once read");
    return Status::Ok;
}

Status cmd_coredump_erase(Args const&, Sink& out) {
    const Status st = hal::sys::coredump_erase();
    out.line(st == Status::Ok           ? "coredump erased"
             : st == Status::NotPresent ? "no coredump partition here"
                                        : "erase failed");
    return st;
}

Status cmd_ev_dump(Args const& a, Sink& out) {
    unsigned long last = evtrace::kEntries;
    if (a.count() > 0) {
        char* end = nullptr;
        last = std::strtoul(a.arg(0), &end, 10);
        if (!end || *end || last == 0) {
            out.line("usage: sys ev dump [<last n>]");
            return Status::BadArg;
        }
    }
    auto* e = new (std::nothrow) evtrace::Entry[evtrace::kEntries];
    if (!e) {
        out.line("out of memory");
        return Status::Failed;
    }
    const std::size_t n = evtrace::snapshot(e, evtrace::kEntries);
    const std::size_t from = n > last ? n - last : 0;
    const auto s = evtrace::stats();
    out.printf("event ring: %zu entr%s held, %" PRIu32 " recorded this boot (+%" PRIu32
               " repeats not stored), %" PRIu32 " carried from the last (%s start)",
               n, n == 1 ? "y" : "ies", s.recorded, s.repeats, s.carried, s.warm ? "warm" : "cold");
    out.line("     seq        ms  ao        event        data");
    bool prev = from < n && e[from].prev;
    if (prev) out.line("-- previous boot --");
    for (std::size_t k = from; k < n; ++k) {
        if (prev && !e[k].prev) {
            out.printf("-- this boot (reset: %s) --",
                       hal::sys::reset_name(hal::sys::info().reset_reason));
            prev = false;
        }
        out.printf("%8" PRIu32 "  %8" PRIu32 "  %-8s  %-11s  %02x %02x %02x %02x", e[k].seq,
                   e[k].ms, evtrace::source_name(e[k].src), evtrace::tag_name(e[k].tag),
                   e[k].data[0], e[k].data[1], e[k].data[2], e[k].data[3]);
    }
    delete[] e;
    return Status::Ok;
}

Status cmd_ev_clear(Args const&, Sink& out) {
    evtrace::clear();
    out.line("event ring cleared");
    return Status::Ok;
}

// ---- tables ---------------------------------------------------------------------------

constexpr CmdSpec kTop[] = {
    {"help", nullptr, "", "[<group> [<verb>]]", "list groups, or a group's commands", ReleaseOk,
     cmd_help},
    {"unsafe", nullptr, "", "[on|off]", "gate hardware-touching commands", None, cmd_unsafe},
};

constexpr CmdSpec kSys[] = {
    {"sys", nullptr, "stat", "", "one-screen: what is it doing right now", ReleaseOk, cmd_stat},
    {"sys", nullptr, "ver", "", "app / build / sdk identity", ReleaseOk, cmd_ver},
    {"sys", nullptr, "snap", "[--hex]", "everything, timestamped: the app's status record",
     ReleaseOk, cmd_sys_snap},
    {"sys", nullptr, "debug", "[<module|glob|all> <level>]", "show or set per-module log levels",
     ReleaseOk, cmd_debug},
    {"sys", nullptr, "top", "[<ms>]", "per-task CPU over a window, stack high-water, core",
     ReleaseOk, cmd_top},
    {"sys", nullptr, "heap", "", "internal + PSRAM, largest block, min", ReleaseOk, cmd_heap},
    {"sys", nullptr, "journal", "[flush]", "the debug log: RAM ring + this boot's file on the card",
     ReleaseOk, cmd_journal},
    {"sys", "journal", "files", "", "journal files on the card (`=file=` pairs), oldest first",
     ReleaseOk, cmd_journal_files},
    {"sys", "journal", "fetch", "<name> [<off>] | stop", "send a journal file on `bulk`", ReleaseOk,
     cmd_journal_fetch},
    {"sys", nullptr, "wd", "", "supervisor: hands / AO stalls, log a heartbeat now", ReleaseOk,
     cmd_wd},
    {"sys", nullptr, "fault", "[ack]", "fault codes on the status row; ack hides them", ReleaseOk,
     cmd_fault},
    {"sys", nullptr, "reboot", "[ota|dfu]", "restart the whole image", Unsafe, cmd_reboot},
    {"sys", "coredump", "info", "", "is there a coredump, and from what", ReleaseOk,
     cmd_coredump_info},
    {"sys", "coredump", "erase", "", "forget the coredump once it has been read", Unsafe,
     cmd_coredump_erase},
    {"sys", "ev", "dump", "[<last n>]", "the 256-entry RTC event ring, oldest first", ReleaseOk,
     cmd_ev_dump},
    {"sys", "ev", "clear", "", "empty the event ring", ReleaseOk, cmd_ev_clear},
};

}  // namespace

extern const CmdTable kTableTop{kTop, sizeof(kTop) / sizeof(kTop[0])};
extern const CmdTable kTableSys{kSys, sizeof(kSys) / sizeof(kSys[0])};

BuildInfo const& build_info() noexcept { return kBuild; }

}  // namespace clk::cli
