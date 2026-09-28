// The `storage` group -- the card and what is on it.                [FIRMWARE.md §9.3, §6.3]
//
// `storage` owns SPI2 (§3.2), so mounting and playing are REQUESTS to the AO and these rows
// wait for its answer.  The exception is reading a directory, which `ls` does on the CLI
// thread: it is a bench listing, FATFS is re-entrant, and a 2 s ring does not notice.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/wav.hpp"
#include "clk/services/storage.hpp"
#include "sto_wait.hpp"

namespace clk::cli {

Status sto_await(uint32_t seq, const char* what, Sink& out) noexcept {
    if (seq == 0) {
        out.printf("%s refused: busy -- storage has four requests queued already", what);
        return Status::Busy;
    }
    // A mount on a slow card is the long pole: ~200 ms of SPI init.  Two seconds is ten of
    // them; past that the AO is stuck, and saying so beats hanging the console.
    for (int i = 0; i < 400; ++i) {
        const auto s = svc::storage().snapshot();
        if (static_cast<int32_t>(s.done_seq - seq) >= 0) {
            if (s.last_st == Status::Ok) return Status::Ok;
            out.printf("%s refused: %s%s%s", what, cmd::name(s.last_st), s.last_why ? " -- " : "",
                       s.last_why ? s.last_why : "");
            if (s.last_wav != hal::wav::Err::Ok) {
                out.printf("  the header says: %s", hal::wav::name(s.last_wav));
                out.line("  the clock plays 48000 Hz, mono, 16-bit PCM WAV and nothing else:");
                out.line("    ffmpeg -i in.mp3 -ac 1 -ar 48000 -c:a pcm_s16le -bitexact out.wav");
            }
            if (s.last_st == Status::NotPresent && !s.mounted)
                out.line("  no card: `storage sd mount` after inserting one");
            return s.last_st;
        }
        hal::clock_::sleep_ms(5);
    }
    out.printf("%s refused: storage never answered (`sys top` -- is the AO running?)", what);
    return Status::Failed;
}

void fmt_ms(char* buf, std::size_t cap, uint32_t ms) noexcept {
    if (ms < 60000) {
        std::snprintf(buf, cap, "%lu.%lu s", static_cast<unsigned long>(ms / 1000),
                      static_cast<unsigned long>(ms % 1000 / 100));
    } else {
        std::snprintf(buf, cap, "%lu min %02lu s", static_cast<unsigned long>(ms / 60000),
                      static_cast<unsigned long>(ms / 1000 % 60));
    }
}

namespace {

const char* playing_name(svc::Storage::Playing p) noexcept {
    switch (p) {
        case svc::Storage::Playing::File:
            return "file";
        case svc::Storage::Playing::Beep:
            return "beep";
        default:
            return "nothing";
    }
}

Status mount_first(Sink& out) {
    if (svc::storage().snapshot().mounted) return Status::Ok;
    return sto_await(svc::storage().mount(), "mount", out);
}

Status cmd_status(Args const&, Sink& out) {
    const auto s = svc::storage().snapshot();
    if (s.mounted) {
        out.printf("card   %s  %" PRIu64 " MB, %" PRIu64 " MB free, %lu kHz", s.card.name,
                   s.card.total_bytes >> 20, s.card.free_bytes >> 20,
                   static_cast<unsigned long>(s.card.freq_khz));
    } else {
        out.line("card   not mounted (no card, or not tried since it went in: `storage sd mount`)");
    }
    out.printf("alarm  tone %s", s.alarm_tone[0] ? s.alarm_tone : "(none -- the beep)");
    char pos[24], len[24];
    fmt_ms(pos, sizeof pos, static_cast<uint32_t>(uint64_t{s.pos_bytes} * 1000u / 2u / 48000u));
    fmt_ms(len, sizeof len, static_cast<uint32_t>(uint64_t{s.data_bytes} * 1000u / 2u / 48000u));
    switch (s.playing) {
        case svc::Storage::Playing::File:
            out.printf("play   %s%s%s  read %s of %s, %lu loops, %lu underruns", s.file,
                       s.loop ? " (loop)" : "", s.alarm ? " [ALARM]" : "", pos, len,
                       static_cast<unsigned long>(s.loops),
                       static_cast<unsigned long>(s.underruns));
            break;
        case svc::Storage::Playing::Beep:
            out.printf("play   the fallback beep%s", s.alarm ? " [ALARM]" : "");
            if (s.last_why) out.printf("       because: %s", s.last_why);
            break;
        default:
            out.printf("play   %s", playing_name(s.playing));
            break;
    }
    const auto st = hal::audio::stream();
    if (st.open) {
        out.printf("ring   %lu / %lu samples (%lu ms)%s", static_cast<unsigned long>(st.level),
                   static_cast<unsigned long>(st.cap),
                   static_cast<unsigned long>(uint64_t{st.level} * 1000u / hal::audio::kRateHz),
                   st.primed ? "" : "  priming");
    }
    return Status::Ok;
}

Status cmd_sd(Args const&, Sink& out) {
    const auto s = svc::storage().snapshot();
    if (!s.mounted) {
        out.line("sd     not mounted -- `storage sd mount`");
        out.line("  there is no card-detect line: a card is only ever found by mounting it");
        return Status::NotPresent;
    }
    out.printf("sd     %s mounted at %s, %" PRIu64 " MB, %" PRIu64 " MB free, SPI2 %lu kHz",
               s.card.name, hal::sd::kRoot, s.card.total_bytes >> 20, s.card.free_bytes >> 20,
               static_cast<unsigned long>(s.card.freq_khz));
    return Status::Ok;
}

Status cmd_mount(Args const&, Sink& out) {
    if (const Status st = sto_await(svc::storage().mount(), "mount", out); st != Status::Ok)
        return st;
    return cmd_sd(Args{}, out);
}

Status cmd_unmount(Args const&, Sink& out) {
    if (const Status st = sto_await(svc::storage().unmount(), "unmount", out); st != Status::Ok)
        return st;
    out.line("sd     unmounted -- safe to pull");
    return Status::Ok;
}

struct Listing {
    Sink* out;
    const char* dir;
    int files, dirs, good;
};

bool has_wav_ext(const char* n) noexcept {
    const std::size_t l = std::strlen(n);
    return l > 4 && (std::strcmp(n + l - 4, ".wav") == 0 || std::strcmp(n + l - 4, ".WAV") == 0);
}

bool list_one(hal::sd::Entry const& e, void* ctx) {
    auto& L = *static_cast<Listing*>(ctx);
    // macOS litters every card it touches with `._name` AppleDouble files that are named
    // like the tone and are not audio.  Hidden, as Finder hides them.
    if (e.name[0] == '.') return true;
    if (e.dir) {
        ++L.dirs;
        L.out->printf("  %-32s  <dir>", e.name);
        return true;
    }
    ++L.files;
    if (!has_wav_ext(e.name)) {
        L.out->printf("  %-32s  %8lu B", e.name, static_cast<unsigned long>(e.size));
        return true;
    }
    char path[hal::sd::kNameMax + 32];
    std::snprintf(path, sizeof path, "%s/%s", L.dir, e.name);
    Status st = Status::Ok;
    const auto in = svc::Storage::probe(path, st);
    if (in.ok()) {
        ++L.good;
        char len[24];
        fmt_ms(len, sizeof len, in.ms());
        L.out->printf("  %-32s  %8lu B  %s", e.name, static_cast<unsigned long>(e.size), len);
    } else {
        L.out->printf("  %-32s  %8lu B  ✗ %s", e.name, static_cast<unsigned long>(e.size),
                      st == Status::BadArg ? hal::wav::name(in.err) : cmd::name(st));
    }
    return true;
}

Status cmd_ls(Args const& a, Sink& out) {
    if (const Status st = mount_first(out); st != Status::Ok) return st;
    char dir[96];
    const char* want = a.arg(0) ? a.arg(0) : hal::sd::kTonesDir;
    if (want[0] != '/') {
        std::snprintf(dir, sizeof dir, "%s/%s", hal::sd::kRoot, want);
    } else {
        std::snprintf(dir, sizeof dir, "%s", want);
    }
    Listing L{&out, dir, 0, 0, 0};
    out.printf("%s", dir);
    const Status st = hal::sd::list(dir, &list_one, &L);
    if (st == Status::Failed) {
        out.printf("ls refused: no directory %s", dir);
        if (std::strcmp(dir, hal::sd::kTonesDir) == 0)
            out.line("  alarm tones go in /tones at the top of the card -- create it");
        return st;
    }
    if (st != Status::Ok) {
        out.printf("ls refused: %s", cmd::name(st));
        return st;
    }
    if (std::strcmp(dir, hal::sd::kTonesDir) == 0) {
        out.printf("%d file%s, %d playable (48 kHz mono 16-bit)%s", L.files,
                   L.files == 1 ? "" : "s", L.good,
                   L.good ? " -- `chrono alarm tone <name>` to choose one" : "");
    } else {
        out.printf("%d file%s, %d dir%s", L.files, L.files == 1 ? "" : "s", L.dirs,
                   L.dirs == 1 ? "" : "s");
    }
    return Status::Ok;
}

Status cmd_stat(Args const& a, Sink& out) {
    if (!a.arg(0)) {
        out.line("usage: storage stat <file>   (a name in /sd/tones, or a /sd/... path)");
        return Status::BadArg;
    }
    if (const Status st = mount_first(out); st != Status::Ok) return st;
    char path[96];
    if (!svc::Storage::resolve(a.arg(0), path, sizeof path)) {
        out.line("stat refused: not a path under /sd");
        return Status::BadArg;
    }
    Status st = Status::Ok;
    const auto in = svc::Storage::probe(path, st);
    if (st != Status::Ok && st != Status::BadArg) {
        out.printf("stat refused: %s -- no such file?", cmd::name(st));
        return st;
    }
    out.printf("%s", path);
    out.printf("  fmt   format %u, %u ch, %lu Hz, %u-bit", in.format, in.channels,
               static_cast<unsigned long>(in.rate), in.bits);
    if (in.data_off) {
        char len[24];
        fmt_ms(len, sizeof len, in.ms());
        out.printf("  data  %lu bytes at offset %lu = %s",
                   static_cast<unsigned long>(in.data_bytes),
                   static_cast<unsigned long>(in.data_off), len);
    }
    out.printf("  %s", in.ok() ? "✓ playable" : hal::wav::name(in.err));
    return in.ok() ? Status::Ok : Status::BadArg;
}

// ---- the app's half (app/PROTOCOL.md "Sound files") -----------------------------------------
// These answer in `=` pairs, which the protocol documents as stable.  The `|` lines around
// them are for a human and may change.

bool parse_u32(const char* s, int base, uint32_t& out) noexcept {
    if (!s || !*s) return false;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s, &end, base);
    if (end == s || *end != '\0' || v > 0xFFFFFFFFull) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

struct Tones {
    Sink* out;
    int n;
};

bool tone_kv(hal::sd::Entry const& e, void* ctx) {
    auto& T = *static_cast<Tones*>(ctx);
    if (e.dir || e.name[0] == '.' || !has_wav_ext(e.name)) return true;
    char path[hal::sd::kNameMax + 32];
    std::snprintf(path, sizeof path, "%s/%s", hal::sd::kTonesDir, e.name);
    Status st = Status::Ok;
    const auto in = svc::Storage::probe(path, st);
    const char* code =
        st == Status::Ok || st == Status::BadArg ? hal::wav::code(in.err) : "unreadable";
    // <bytes>/<ms>/<state>/<name> -- the name LAST, because it is the only field that may
    // contain anything (except '/', which a file name cannot).
    char v[hal::sd::kNameMax + 48];
    std::snprintf(v, sizeof v, "%lu/%lu/%s/%s", static_cast<unsigned long>(e.size),
                  static_cast<unsigned long>(in.ms()), code, e.name);
    T.out->kv("tone", v);
    ++T.n;
    return true;
}

Status cmd_tones(Args const&, Sink& out) {
    if (const Status st = mount_first(out); st != Status::Ok) return st;
    const auto s = svc::storage().snapshot();
    char v[64];
    std::snprintf(v, sizeof v, "%" PRIu64 "/%" PRIu64, s.card.total_bytes, s.card.free_bytes);
    out.kv("card", v);
    out.kv("alarm", s.alarm_tone);
    Tones T{&out, 0};
    const Status st = hal::sd::list(hal::sd::kTonesDir, &tone_kv, &T);
    // No /tones yet is an empty list, not an error: the first upload creates it.
    if (st != Status::Ok && st != Status::Failed) return st;
    out.printf("%d tone%s", T.n, T.n == 1 ? "" : "s");
    return Status::Ok;
}

Status cmd_rm(Args const& a, Sink& out) {
    if (!a.arg(0) || a.arg(1)) {
        out.line("usage: storage rm <name>   (a file in /sd/tones)");
        return Status::BadArg;
    }
    if (const Status st = sto_await(svc::storage().remove(a.arg(0)), "rm", out); st != Status::Ok)
        return st;
    const auto s = svc::storage().snapshot();
    out.printf("removed %s%s%s", a.arg(0), s.last_why ? " -- " : "", s.last_why ? s.last_why : "");
    return Status::Ok;
}

void put_kv(Sink& out) {
    const auto s = svc::storage().snapshot();
    char v[16];
    std::snprintf(v, sizeof v, "%lu", static_cast<unsigned long>(s.put_next));
    out.kv("next", v);
    std::snprintf(v, sizeof v, "%lu", static_cast<unsigned long>(s.put_size));
    out.kv("size", v);
}

// `storage put` alone: is an upload open, and where does it continue.
// `storage put <name> <size> <crc32>`: open one (or resume the same one).
Status cmd_put(Args const& a, Sink& out) {
    if (!a.arg(0)) {
        const auto s = svc::storage().snapshot();
        if (!s.put_open) {
            out.line("no upload open");
            return Status::NotReady;
        }
        out.kv("name", s.put_name);
        put_kv(out);
        if (s.put_failed) out.line("⚠ the card refused a write -- `storage put abort`");
        return Status::Ok;
    }
    uint32_t size = 0, crc = 0;
    if (!parse_u32(a.arg(1), 10, size) || !parse_u32(a.arg(2), 16, crc) || a.arg(3)) {
        out.line("usage: storage put <name.wav> <size_bytes> <crc32_hex>");
        return Status::BadArg;
    }
    if (const Status st = sto_await(svc::storage().put_begin(a.arg(0), size, crc), "put", out);
        st != Status::Ok)
        return st;
    const auto s = svc::storage().snapshot();
    put_kv(out);
    out.printf("put %s: %s at %lu of %lu -- data goes to the `blob` characteristic", s.put_name,
               s.last_why ? s.last_why : "open", static_cast<unsigned long>(s.put_next),
               static_cast<unsigned long>(s.put_size));
    return Status::Ok;
}

Status cmd_put_end(Args const&, Sink& out) {
    const auto before = svc::storage().snapshot();
    const Status st = sto_await(svc::storage().put_end(), "put end", out);
    if (st == Status::NotReady && before.put_open) put_kv(out);  // short: where to resume
    if (st != Status::Ok) return st;
    out.printf("put %s: done, in /sd/tones", before.put_name);
    return Status::Ok;
}

Status cmd_put_abort(Args const&, Sink& out) {
    if (const Status st = sto_await(svc::storage().put_abort(), "put abort", out); st != Status::Ok)
        return st;
    out.line("upload discarded");
    return Status::Ok;
}

// The `blob` characteristic's path, from the console: one write of hex bytes at an offset.
// For the bench and the tests -- 100 bytes a line is not how anybody should send a file.
Status cmd_put_data(Args const& a, Sink& out) {
    uint32_t off = 0;
    const char* hex = a.arg(1);
    const std::size_t hl = hex ? std::strlen(hex) : 0;
    if (!parse_u32(a.arg(0), 10, off) || hl == 0 || hl % 2 || hl / 2 > hal::ble::kMaxBlob - 4) {
        out.line("usage: storage put data <offset> <hex bytes>");
        return Status::BadArg;
    }
    uint8_t blob[hal::ble::kMaxBlob];
    for (int i = 0; i < 4; ++i) blob[i] = static_cast<uint8_t>(off >> (8 * i));
    for (std::size_t i = 0; i < hl / 2; ++i) {
        const char b[3] = {hex[2 * i], hex[2 * i + 1], 0};
        char* end = nullptr;
        blob[4 + i] = static_cast<uint8_t>(std::strtoul(b, &end, 16));
        if (*end) {
            out.line("put data refused: not hex");
            return Status::BadArg;
        }
    }
    const Status st = svc::storage().put_data(blob, 4 + hl / 2);
    if (st != Status::Ok) {
        out.printf("put data refused: %s (ATT error 0x%02X on `blob`)", cmd::name(st),
                   hal::ble::blob_att_err(st));
        put_kv(out);
        return st;
    }
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"storage", nullptr, "status", "", "card, alarm tone, what is streaming", ReleaseOk,
     cmd_status},
    {"storage", nullptr, "ls", "[<path>]", "list a directory (default /sd/tones)", ReleaseOk,
     cmd_ls},
    {"storage", nullptr, "stat", "<file>", "one WAV header, checked", ReleaseOk, cmd_stat},
    {"storage", "sd", "", "", "is a card mounted, how big", ReleaseOk, cmd_sd},
    {"storage", "sd", "mount", "", "mount the card (no card-detect: try it)", ReleaseOk, cmd_mount},
    {"storage", "sd", "unmount", "", "unmount before pulling the card", ReleaseOk, cmd_unmount},
    {"storage", nullptr, "tones", "", "the tone list as `=` pairs (the app's)", ReleaseOk,
     cmd_tones},
    {"storage", nullptr, "rm", "<name>", "delete a tone from /sd/tones", ReleaseOk, cmd_rm},
    {"storage", "put", "", "[<name> <size> <crc32>]", "open/resume an upload, or show it",
     ReleaseOk, cmd_put},
    {"storage", "put", "end", "", "check length, CRC, header; move into place", ReleaseOk,
     cmd_put_end},
    {"storage", "put", "abort", "", "discard the open upload", ReleaseOk, cmd_put_abort},
    {"storage", "put", "data", "<offset> <hex>", "one `blob` write, from the console", None,
     cmd_put_data},
};
static_assert(sizeof(kRows) / sizeof(kRows[0]) == 12, "added a handler? add its row too");

}  // namespace

extern const CmdTable kTableStorage{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
