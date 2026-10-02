#include "clk/services/storage.hpp"

#include <cstdio>
#include <cstring>

#include "clk/log.hpp"

namespace clk::svc {
namespace {

// NVS (§7.5).  The alarm tone, as a bare name under /sd/tones.
constexpr const char* kKeyTone = "sto.tone";

// §3.2's table: storage(14), below audio(18) and motion(20).  The stream is protected by the
// 2 s ring, not by priority -- a 10 ms tick that is late by a whole hand move costs nothing.
constexpr int kPrio = 14;
constexpr std::size_t kStack = 6144;
constexpr uint32_t kPumpMs = 10;
constexpr uint32_t kIdleMs = 500;

// The fallback, in the one voice that needs no file.  Two short notes and a rest, repeating
// -- recognisably an alarm and not the 440 Hz preview chime.  The period keeps the gap under
// hal::audio's 500 ms idle park, so the amp is not cycled (and SPK_SD does not tick) per note.
constexpr uint32_t kBeepHz = 880;
constexpr uint32_t kBeepMs = 120;
constexpr uint32_t kBeepGapMs = 200;  // note start to note start
constexpr uint32_t kBeepPeriodMs = 800;

void copy_name(char* dst, const char* src) noexcept {
    std::snprintf(dst, Storage::kNameMax, "%s", src ? src : "");
}

}  // namespace

Storage::Storage() noexcept : ActiveObject({"storage", kPrio, kStack, kIdleMs}) {}

Storage& storage() noexcept {
    static Storage s;
    return s;
}

// ---- static helpers ------------------------------------------------------------------------

bool Storage::resolve(const char* name, char* out, std::size_t cap) noexcept {
    if (!name || !*name || !out) return false;
    int n = 0;
    if (name[0] == '/') {
        if (std::strncmp(name, hal::sd::kRoot, 3) != 0 || (name[3] != '/' && name[3] != '\0'))
            return false;
        n = std::snprintf(out, cap, "%s", name);
    } else {
        n = std::snprintf(out, cap, "%s/%s", hal::sd::kTonesDir, name);
    }
    return n > 0 && static_cast<std::size_t>(n) < cap;
}

hal::wav::Info Storage::probe(const char* path, Status& st) noexcept {
    hal::wav::Info in{};
    const auto fd = hal::sd::open(path);
    if (!fd.ok()) {
        st = fd.st;
        return in;
    }
    uint8_t hdr[hal::wav::kHeaderMax];
    const auto sz = hal::sd::size(fd.v);
    const auto r = hal::sd::read(fd.v, hdr, sizeof hdr);
    hal::sd::close(fd.v);
    if (!sz.ok() || !r.ok()) {
        st = Status::Failed;
        return in;
    }
    in = hal::wav::parse(hdr, r.v, sz.v);
    st = in.ok() ? Status::Ok : Status::BadArg;
    return in;
}

// ---- requests ------------------------------------------------------------------------------

uint32_t Storage::enqueue(Kind k, const char* name, bool loop, uint32_t size,
                          uint32_t crc) noexcept {
    uint32_t seq = 0;
    {
        port::Lock lk{mx_};
        if (q_n_ == kQueue) return 0;
        seq = next_seq_++;
        if (next_seq_ == 0) next_seq_ = 1;  // 0 is "not queued"
        Req& r = q_[q_n_++];
        r.kind = k;
        r.loop = loop;
        r.seq = seq;
        r.size = size;
        r.crc = crc;
        copy_name(r.name, name);
    }
    post(StoRx{});
    return seq;
}

uint32_t Storage::play(const char* name, bool loop) noexcept {
    return enqueue(Kind::Play, name, loop);
}
uint32_t Storage::ring_alarm() noexcept { return enqueue(Kind::Alarm, nullptr, true); }
uint32_t Storage::stop() noexcept { return enqueue(Kind::Stop, nullptr, false); }
uint32_t Storage::mount() noexcept { return enqueue(Kind::Mount, nullptr, false); }
uint32_t Storage::unmount() noexcept { return enqueue(Kind::Unmount, nullptr, false); }
uint32_t Storage::select_tone(const char* name) noexcept {
    return enqueue(Kind::Select, name, false);
}
uint32_t Storage::remove(const char* name) noexcept { return enqueue(Kind::Remove, name, false); }
uint32_t Storage::put_begin(const char* name, uint32_t size, uint32_t crc) noexcept {
    return enqueue(Kind::PutBegin, name, false, size, crc);
}
uint32_t Storage::put_end() noexcept { return enqueue(Kind::PutEnd, nullptr, false); }
uint32_t Storage::put_abort() noexcept { return enqueue(Kind::PutAbort, nullptr, false); }
uint32_t Storage::log_flush() noexcept { return enqueue(Kind::LogFlush, nullptr, false); }
uint32_t Storage::log_fetch(uint32_t day, uint32_t offset) noexcept {
    return enqueue(Kind::LogFetch, nullptr, false, day, offset);
}
uint32_t Storage::log_fetch_stop() noexcept { return enqueue(Kind::LogFetchStop, nullptr, false); }
uint32_t Storage::dbg_fetch(const char* name, uint32_t offset) noexcept {
    return enqueue(Kind::DbgFetch, name, false, offset);
}

void Storage::retick() noexcept {
    set_tick(playing_ != Playing::Nothing || fetch_fd_ >= 0 ? kPumpMs : kIdleMs);
}

// ---- uploads ---------------------------------------------------------------------------------

// CRC-32/ISO-HDLC -- zlib's crc32(), which is what the phone (and Python's zlib) computes.
// Nibble table: 64 bytes of flash, two lookups a byte, quick enough for 10 KB/s of BLE.
uint32_t Storage::crc32(uint32_t crc, const uint8_t* p, std::size_t n) noexcept {
    static constexpr uint32_t kT[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
        0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    uint32_t c = ~crc;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        c = (c >> 4) ^ kT[c & 15u];
        c = (c >> 4) ^ kT[c & 15u];
    }
    return ~c;
}

bool Storage::valid_upload_name(const char* name) noexcept {
    if (!name) return false;
    const std::size_t n = std::strlen(name);
    if (n < 5 || n >= kNameMax || name[0] == '.') return false;
    for (std::size_t i = 0; i < n; ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        // FAT's forbidden set, plus control characters.  UTF-8 above 0x7F is fine (LFN).
        if (c < 0x20 || std::strchr("/\\:*?\"<>|", c)) return false;
    }
    const char* ext = name + n - 4;
    return (ext[0] == '.') && (ext[1] == 'w' || ext[1] == 'W') &&
           (ext[2] == 'a' || ext[2] == 'A') && (ext[3] == 'v' || ext[3] == 'V');
}

// BLE host task.  Copy and return -- the Status is the ATT response the phone gets.
Status Storage::put_data(const uint8_t* blob, std::size_t len) noexcept {
    if (!blob || len <= 4 || len - 4 > hal::ble::kMaxBlob) return Status::BadArg;
    const uint32_t off = static_cast<uint32_t>(blob[0]) | static_cast<uint32_t>(blob[1]) << 8 |
                         static_cast<uint32_t>(blob[2]) << 16 |
                         static_cast<uint32_t>(blob[3]) << 24;
    const std::size_t n = len - 4;
    {
        port::Lock lk{mx_};
        if (!put_open_) return Status::NotReady;
        if (put_failed_) return Status::Failed;
        // Strictly sequential.  A repeat of the last write (its response was lost) is also
        // refused here, and the phone recovers by asking `storage put` for `next`.
        if (off != put_next_ || n > put_size_ - put_next_) return Status::BadArg;
        if (chunk_n_ == kChunks) return Status::Busy;
        Chunk& c = chunks_[(chunk_head_ + chunk_n_) % kChunks];
        std::memcpy(c.data, blob + 4, n);
        c.len = static_cast<uint16_t>(n);
        ++chunk_n_;
        put_next_ += static_cast<uint32_t>(n);
    }
    post(StoRx{});
    return Status::Ok;
}

// This AO.  The slot at the head stays counted while it is written, so put_data() cannot
// reuse it; it is released only after the card has it.
void Storage::drain_chunks() noexcept {
    for (;;) {
        Chunk* c = nullptr;
        {
            port::Lock lk{mx_};
            if (chunk_n_ == 0) return;
            c = &chunks_[chunk_head_];
        }
        bool ok = put_fd_ >= 0;
        if (ok) {
            const auto r = hal::sd::write(put_fd_, c->data, c->len);
            ok = r.ok() && r.v == c->len;
            if (ok) {
                put_crc_ = crc32(put_crc_, c->data, c->len);
                put_written_ += c->len;
            } else {
                CLK_LOGW(storage, "put %s: card write failed at %lu", put_name_,
                         static_cast<unsigned long>(put_written_));
            }
        }
        port::Lock lk{mx_};
        if (!ok) put_failed_ = true;
        chunk_head_ = (chunk_head_ + 1) % kChunks;
        --chunk_n_;
    }
}

void Storage::put_path(char* out, std::size_t cap) const noexcept {
    std::snprintf(out, cap, "%s/.%s.part", hal::sd::kTonesDir, put_name_);
}

void Storage::put_close(bool discard) noexcept {
    if (put_fd_ >= 0) hal::sd::close(put_fd_);
    put_fd_ = -1;
    if (discard && put_name_[0]) {
        char part[128];
        put_path(part, sizeof part);
        (void)hal::sd::remove(part);
    }
    put_name_[0] = '\0';
    put_written_ = 0;
    put_crc_ = 0;
    port::Lock lk{mx_};
    put_open_ = false;
    put_failed_ = false;
    put_next_ = 0;
    put_size_ = 0;
    chunk_n_ = 0;
}

Storage::Snapshot Storage::snapshot() const noexcept {
    port::Lock lk{mx_};
    return snap_;
}

void Storage::answer(uint32_t seq, Status st, const char* why, hal::wav::Err werr) noexcept {
    {
        port::Lock lk{mx_};
        snap_.last_st = st;
        snap_.last_why = why;
        snap_.last_wav = werr;
    }
    publish();
    // Published LAST, as hal::audio does with start_seq: a waiter that sees its sequence
    // number also sees the answer and the state that produced it.
    port::Lock lk{mx_};
    snap_.done_seq = seq;
}

// ---- the AO ---------------------------------------------------------------------------------

void Storage::on_start() {
    if (hal::store::get_str(kKeyTone, tone_, sizeof tone_) != Status::Ok) tone_[0] = '\0';
    const Status st = ensure_mounted();
    CLK_LOGI(storage, "up; card %s, alarm tone %s", st == Status::Ok ? "mounted" : clk::name(st),
             tone_[0] ? tone_ : "(none -- the beep)");
    log_start();
    dbg_start();
    publish();
}

void Storage::on_event(Event const& e) {
    if (!as<StoRx>(e)) return;
    // Upload data first, and before every request: a `put end` must see every byte that was
    // accepted ahead of it.
    drain_chunks();
    for (;;) {
        Req r{};
        {
            port::Lock lk{mx_};
            if (q_n_ == 0) break;
            r = q_[0];
            for (std::size_t i = 1; i < q_n_; ++i) q_[i - 1] = q_[i];
            --q_n_;
        }
        drain_chunks();
        handle(r);
    }
}

void Storage::on_tick() {
    drain_chunks();  // a StoRx lost to a full mailbox must not strand accepted data
    if (playing_ == Playing::File) pump();
    if (playing_ == Playing::Beep) beep_tick();
    log_tick();
    dbg_tick();
    publish();
}

Status Storage::ensure_mounted() noexcept {
    // There is no card-detect line, so the only honest question is "does it mount NOW" --
    // and a card that was pulled since shows up as a mount the VFS no longer backs.
    mounted_ = hal::sd::mounted();
    if (mounted_) return Status::Ok;
    const Status st = hal::sd::mount();
    mounted_ = st == Status::Ok;
    // Asked once, at mount, and cached: the free-space count is a FAT walk when the card's
    // FSINFO cannot be trusted, and nothing on this device writes the card anyway.
    card_ = {};
    if (mounted_) {
        if (const auto in = hal::sd::info(); in.ok()) card_ = in.v;
    }
    return st;
}

void Storage::handle(Req const& r) noexcept {
    switch (r.kind) {
        case Kind::Mount: {
            const Status st = ensure_mounted();
            return answer(r.seq, st, st == Status::Ok ? nullptr : "no card answered");
        }
        case Kind::Unmount: {
            if (put_fd_ >= 0) put_close(true);  // a file half-written to a card being pulled
            log_fetch_end("card unmounted");
            const bool ringing = alarm_;
            if (playing_ == Playing::File) halt(false);
            const Status st = hal::sd::unmount();
            mounted_ = false;
            if (ringing && playing_ == Playing::Nothing) start_beep();  // the alarm carries on
            return answer(r.seq, st, nullptr);
        }
        case Kind::Stop:
            halt(true);
            return answer(r.seq, Status::Ok, nullptr);
        case Kind::Select: {
            if (!r.name[0]) {
                tone_[0] = '\0';
                const Status st = hal::store::set_str(kKeyTone, "");
                CLK_LOGI(storage, "alarm tone cleared -- the beep");
                return answer(r.seq, st == Status::NotPresent ? Status::Ok : st, nullptr);
            }
            if (std::strchr(r.name, '/'))
                return answer(r.seq, Status::BadArg, "a bare name under /sd/tones, no path");
            if (const Status st = ensure_mounted(); st != Status::Ok)
                return answer(r.seq, st, "no card");
            char path[96];
            if (!resolve(r.name, path, sizeof path))
                return answer(r.seq, Status::BadArg, "name too long");
            Status st = Status::Ok;
            const auto in = probe(path, st);
            if (st != Status::Ok && st != Status::BadArg)
                return answer(r.seq, st, "no such file in /sd/tones");
            if (!in.ok()) return answer(r.seq, Status::BadArg, "not a usable WAV", in.err);
            copy_name(tone_, r.name);
            // NotPresent = no NVS on this build (the host test rig).  The selection still
            // holds until reboot, which is all a store-less binary can offer.
            const Status sst = hal::store::set_str(kKeyTone, tone_);
            CLK_LOGI(storage, "alarm tone: %s (%lu ms)", tone_,
                     static_cast<unsigned long>(in.ms()));
            return answer(r.seq, sst == Status::NotPresent ? Status::Ok : sst,
                          sst == Status::Failed ? "chosen, but NVS would not save it" : nullptr);
        }
        case Kind::Remove: {
            if (!r.name[0] || r.name[0] == '.' || std::strchr(r.name, '/'))
                return answer(r.seq, Status::BadArg, "a bare name under /sd/tones, no path");
            if (const Status st = ensure_mounted(); st != Status::Ok)
                return answer(r.seq, st, "no card");
            char path[96];
            if (!resolve(r.name, path, sizeof path))
                return answer(r.seq, Status::BadArg, "name too long");
            if (playing_ == Playing::File && std::strcmp(file_, r.name) == 0) {
                const bool ringing = alarm_;
                halt(false);
                if (ringing) start_beep();  // the file goes, the alarm does not
            }
            if (hal::sd::remove(path) != Status::Ok)
                return answer(r.seq, Status::Failed, "no such file in /sd/tones");
            const bool was_alarm = std::strcmp(tone_, r.name) == 0;
            if (was_alarm) {
                tone_[0] = '\0';
                (void)hal::store::set_str(kKeyTone, "");
            }
            if (const auto in = hal::sd::info(); in.ok()) card_ = in.v;
            CLK_LOGI(storage, "removed %s%s", path, was_alarm ? " -- it was the alarm tone" : "");
            return answer(r.seq, Status::Ok,
                          was_alarm ? "it was the alarm tone: the beep now" : nullptr);
        }
        case Kind::PutBegin: {
            if (!valid_upload_name(r.name))
                return answer(r.seq, Status::BadArg,
                              "name: a bare name ending .wav, no / \\ : * ? \" < > |");
            if (r.size < 45 || r.size > kMaxUpload)
                return answer(r.seq, Status::BadArg, "size: 45 B .. 16 MB");
            if (const Status st = ensure_mounted(); st != Status::Ok)
                return answer(r.seq, st, "no card");
            bool same = false;
            {
                port::Lock lk{mx_};
                same = put_open_ && !put_failed_ && put_size_ == r.size;
            }
            if (same && std::strcmp(put_name_, r.name) == 0 && put_crc_want_ == r.crc) {
                CLK_LOGI(storage, "put %s: resuming at %lu", put_name_,
                         static_cast<unsigned long>(put_written_));
                return answer(r.seq, Status::Ok, "resumed");
            }
            put_close(true);
            if (hal::sd::mkdir(hal::sd::kTonesDir) != Status::Ok)
                return answer(r.seq, Status::Failed, "could not create /sd/tones");
            if (const auto in = hal::sd::info(); in.ok()) {
                card_ = in.v;
                // Headroom for the FAT and the directory -- a card written to the last cluster
                // is one whose next write fails in the middle of somebody's upload.
                if (in.v.free_bytes < uint64_t{r.size} + 64u * 1024u)
                    return answer(r.seq, Status::Failed, "card full");
            }
            copy_name(put_name_, r.name);
            char part[128];
            put_path(part, sizeof part);
            const auto fd = hal::sd::create(part, false);
            if (!fd.ok()) {
                put_name_[0] = '\0';
                return answer(r.seq, Status::Failed, "could not create the file");
            }
            put_fd_ = fd.v;
            put_crc_want_ = r.crc;
            put_crc_ = 0;
            put_written_ = 0;
            {
                port::Lock lk{mx_};
                put_open_ = true;
                put_failed_ = false;
                put_next_ = 0;
                put_size_ = r.size;
                chunk_n_ = 0;
            }
            CLK_LOGI(storage, "put %s: %lu bytes", put_name_, static_cast<unsigned long>(r.size));
            return answer(r.seq, Status::Ok, nullptr);
        }
        case Kind::PutAbort:
            put_close(true);
            return answer(r.seq, Status::Ok, nullptr);
        case Kind::LogFlush: {
            const Status st = log_write();
            return answer(r.seq, st, st == Status::Ok ? nullptr : "no card");
        }
        case Kind::DbgFlush: {
            const Status st = dbg_write();
            return answer(r.seq, st, st == Status::Ok ? nullptr : "no card");
        }
        case Kind::LogFetch: {
            const Status st = log_fetch_begin(r.size, r.crc);
            const char* why = nullptr;
            if (st == Status::NotPresent) why = "no card";
            if (st == Status::Failed) why = "no log for that day";
            if (st == Status::BadArg) why = "offset past the end of the file";
            return answer(r.seq, st, why);
        }
        case Kind::DbgFetch: {
            uint32_t boot = 0, order = 0;
            if (!dbg_parse(r.name, boot, order))
                return answer(r.seq, Status::BadArg, "not a journal file");
            (void)dbg_write();  // so the current file has everything up to now
            char path[40];
            dbg_path(boot, order, path, sizeof path);
            const Status st = fetch_begin(path, r.size, 0, r.name);
            const char* why = nullptr;
            if (st == Status::NotPresent) why = "no card";
            if (st == Status::Failed) why = "no such file";
            if (st == Status::BadArg) why = "offset past the end of the file";
            return answer(r.seq, st, why);
        }
        case Kind::LogFetchStop:
            log_fetch_end("stopped");
            return answer(r.seq, Status::Ok, nullptr);
        case Kind::PutEnd: {
            bool open = false, failed = false;
            uint32_t size = 0;
            {
                port::Lock lk{mx_};
                open = put_open_;
                failed = put_failed_;
                size = put_size_;
            }
            if (!open) return answer(r.seq, Status::NotReady, "no upload open");
            if (failed) {
                put_close(true);
                return answer(r.seq, Status::Failed, "the card refused a write -- start again");
            }
            // Short: keep it, it can be resumed from `next`.
            if (put_written_ != size)
                return answer(r.seq, Status::NotReady, "not all bytes sent yet");
            hal::sd::close(put_fd_);
            put_fd_ = -1;
            char part[128];
            put_path(part, sizeof part);
            if (put_crc_ != put_crc_want_) {
                put_close(true);
                return answer(r.seq, Status::BadArg, "CRC mismatch -- the bytes arrived damaged");
            }
            Status pst = Status::Ok;
            const auto in = probe(part, pst);
            if (!in.ok()) {
                put_close(true);
                return answer(r.seq, Status::BadArg, "not a usable WAV", in.err);
            }
            char final_path[96];
            (void)resolve(put_name_, final_path, sizeof final_path);
            if (playing_ == Playing::File && std::strcmp(file_, put_name_) == 0) {
                const bool ringing = alarm_;
                halt(false);
                if (ringing) start_beep();
            }
            (void)hal::sd::remove(final_path);  // replacing a file of the same name
            if (hal::sd::rename(part, final_path) != Status::Ok) {
                put_close(true);
                return answer(r.seq, Status::Failed, "could not rename into place");
            }
            CLK_LOGI(storage, "put %s: done, %lu ms of audio", put_name_,
                     static_cast<unsigned long>(in.ms()));
            put_close(false);
            if (const auto ci = hal::sd::info(); ci.ok()) card_ = ci.v;
            return answer(r.seq, Status::Ok, nullptr);
        }
        case Kind::Play: {
            const char* why = nullptr;
            hal::wav::Err werr = hal::wav::Err::Ok;
            const Status st = start_file(r.name, r.loop, false, 0, &why, &werr);
            return answer(r.seq, st, why, werr);
        }
        case Kind::Alarm: {
            const char* why = nullptr;
            hal::wav::Err werr = hal::wav::Err::Ok;
            Status st = Status::NotPresent;
            if (tone_[0]) {
                st = start_file(tone_, true, true, kAlarmRampMs, &why, &werr);
            } else {
                why = "no alarm tone chosen";
            }
            if (st != Status::Ok) {
                // Loud, because on the morning it matters somebody will ask why the clock
                // beeped instead of playing their file, and the answer is this line.
                CLK_LOGW(storage, "alarm: %s%s%s -- falling back to the beep", why ? why : "?",
                         werr != hal::wav::Err::Ok ? ": " : "",
                         werr != hal::wav::Err::Ok ? hal::wav::name(werr) : "");
                start_beep();
                // The alarm IS ringing -- the answer says so, and `last_why` says in what voice.
                return answer(r.seq, Status::Ok, why, werr);
            }
            return answer(r.seq, Status::Ok, nullptr);
        }
    }
}

Status Storage::start_file(const char* name, bool loop, bool alarm, uint32_t ramp_ms,
                           const char** why, hal::wav::Err* werr) noexcept {
    halt(false);
    char path[96];
    if (!resolve(name, path, sizeof path)) {
        *why = "not a path under /sd";
        return Status::BadArg;
    }
    if (const Status st = ensure_mounted(); st != Status::Ok) {
        *why = "no card";
        return st;
    }
    const auto fd = hal::sd::open(path);
    if (!fd.ok()) {
        *why = "no such file";
        return fd.st == Status::NotPresent ? Status::NotPresent : Status::Failed;
    }
    uint8_t hdr[hal::wav::kHeaderMax];
    const auto sz = hal::sd::size(fd.v);
    const auto rd = hal::sd::read(fd.v, hdr, sizeof hdr);
    if (!sz.ok() || !rd.ok()) {
        hal::sd::close(fd.v);
        *why = "card read failed";
        return Status::Failed;
    }
    const auto in = hal::wav::parse(hdr, rd.v, sz.v);
    if (!in.ok()) {
        hal::sd::close(fd.v);
        *why = "not a usable WAV";
        *werr = in.err;
        return Status::BadArg;
    }
    if (in.data_bytes == 0 || hal::sd::seek(fd.v, in.data_off) != Status::Ok) {
        hal::sd::close(fd.v);
        *why = in.data_bytes == 0 ? "the data chunk is empty" : "card seek failed";
        return Status::Failed;
    }
    // The amp starts asynchronously; its failure lands in hal::audio's state and shows up
    // here as a stream that closed itself, which pump() turns into an answer in the log.
    if (const Status st = hal::audio::stream_open(ramp_ms); st != Status::Ok) {
        hal::sd::close(fd.v);
        *why = st == Status::NotPresent ? "no amp fitted" : "the amp would not start";
        return st;
    }
    fd_ = fd.v;
    wav_ = in;
    left_ = in.data_bytes;
    loops_ = 0;
    loop_ = loop;
    alarm_ = alarm;
    eof_sent_ = false;
    copy_name(file_, name);
    stream_seq_ = hal::audio::stream().seq;
    playing_ = Playing::File;
    set_tick(kPumpMs);
    CLK_LOGI(storage, "play %s%s: %lu ms%s", path, loop ? " (loop)" : "",
             static_cast<unsigned long>(in.ms()), ramp_ms ? ", ramping" : "");
    pump();  // prime now rather than 10 ms from now
    return Status::Ok;
}

void Storage::start_beep() noexcept {
    halt(false);
    playing_ = Playing::Beep;
    alarm_ = true;
    loop_ = true;
    copy_name(file_, "");
    beep_t0_us_ = port::now_us();
    beep_step_ = 0;
    set_tick(kPumpMs);
    beep_tick();
}

void Storage::close_file() noexcept {
    if (fd_ >= 0) hal::sd::close(fd_);
    fd_ = -1;
}

void Storage::halt(bool fade) noexcept {
    if (playing_ == Playing::File) {
        close_file();
        // Only if the stream is still ours: something else (`audio tone`) may already own the
        // amp, and stopping it would stop THAT.
        if (hal::audio::stream().seq == stream_seq_) (void)hal::audio::stop();
    } else if (playing_ == Playing::Beep) {
        (void)hal::audio::stop();
    }
    (void)fade;  // both stops fade: hal::audio::stop() always releases through the tail
    playing_ = Playing::Nothing;
    alarm_ = false;
    retick();
}

void Storage::pump() noexcept {
    const auto st = hal::audio::stream();
    if (st.seq != stream_seq_ || !st.open) {
        // Finished, or replaced: a tone took the amp, `audio stop` ran, the amp failed to start.
        if (!eof_sent_) CLK_LOGI(storage, "%s: stream closed before the end", file_);
        close_file();
        playing_ = Playing::Nothing;
        alarm_ = false;
        retick();
        return;
    }
    if (eof_sent_) return;  // draining the last of the ring

    // Top up.  Bounded per tick so one tick never blocks for the whole 2 s ring at once.
    for (int chunk = 0; chunk < 8; ++chunk) {
        std::size_t space = hal::audio::stream_space();
        if (space == 0) return;
        if (left_ == 0) {
            if (!loop_) {
                hal::audio::stream_end();
                eof_sent_ = true;
                return;
            }
            // Seamless: the next pass goes straight into the ring behind this one.
            if (hal::sd::seek(fd_, wav_.data_off) != Status::Ok) break;
            left_ = wav_.data_bytes;
            ++loops_;
        }
        std::size_t want = sizeof buf_ / sizeof buf_[0];
        if (want > space) want = space;
        if (want > left_ / 2u) want = left_ / 2u;
        const auto r = hal::sd::read(fd_, buf_, want * 2u);
        if (!r.ok() || r.v == 0) {
            // The card went away, or the file is shorter than its header said.
            CLK_LOGW(storage, "%s: card read %s at %lu of %lu bytes", file_,
                     r.ok() ? "hit the end" : "FAILED",
                     static_cast<unsigned long>(wav_.data_bytes - left_),
                     static_cast<unsigned long>(wav_.data_bytes));
            if (!r.ok()) mounted_ = false;
            if (alarm_) {
                start_beep();  // never let the alarm go quiet over a file
                return;
            }
            hal::audio::stream_end();
            eof_sent_ = true;
            return;
        }
        const std::size_t n = r.v / 2u;
        left_ -= static_cast<uint32_t>(r.v);
        (void)hal::audio::stream_write(buf_, n);
    }
}

void Storage::beep_tick() noexcept {
    const uint64_t t_ms = (port::now_us() - beep_t0_us_) / 1000ull;
    const uint32_t cycle = static_cast<uint32_t>(t_ms / kBeepPeriodMs);
    const uint32_t in = static_cast<uint32_t>(t_ms % kBeepPeriodMs);
    // Step n = note (n % 2) of cycle (n / 2).  Each fires once, when its time has come.
    const uint32_t due = cycle * 2u + (in >= kBeepGapMs ? 2u : 1u);
    if (due > beep_step_) {
        beep_step_ = due;
        (void)hal::audio::tone(kBeepHz, kBeepMs);
    }
}

void Storage::publish() noexcept {
    Snapshot s{};
    {
        port::Lock lk{mx_};
        s = snap_;
    }
    s.mounted = mounted_ && hal::sd::mounted();
    s.card = s.mounted ? card_ : hal::sd::Info{};
    copy_name(s.alarm_tone, tone_);
    s.playing = playing_;
    s.alarm = alarm_;
    s.loop = loop_;
    copy_name(s.file, file_);
    s.data_bytes = playing_ == Playing::File ? wav_.data_bytes : 0u;
    s.pos_bytes = playing_ == Playing::File ? wav_.data_bytes - left_ : 0u;
    s.loops = loops_;
    copy_name(s.put_name, put_name_);
    if (playing_ == Playing::File) s.underruns = hal::audio::stream().underruns;
    port::Lock lk{mx_};
    s.put_open = put_open_;
    s.put_failed = put_failed_;
    s.put_size = put_size_;
    s.put_next = put_next_;
    // Keep the request answer fields -- they are answer()'s, not ours.
    s.done_seq = snap_.done_seq;
    s.last_st = snap_.last_st;
    s.last_why = snap_.last_why;
    s.last_wav = snap_.last_wav;
    snap_ = s;
}

}  // namespace clk::svc
