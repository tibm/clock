// The card, the alarm tone, and the stream from one to the other.   [FIRMWARE.md §6.3, §6.2]
//
// Owns SPI2 and the FATFS mount.  Every blocking file operation the product makes happens on
// this AO's thread -- that is what §3.2 means by `motion` never waiting on an SD read, and it
// is why playing a file is a REQUEST here rather than a call: the CLI and `ui` queue it and
// read the answer off the snapshot.
//
// Playing a file: open it, check the header (wav.hpp -- 48 kHz mono s16, refused by name
// otherwise), hand hal::audio a fresh stream, and keep its 2 s PSRAM ring topped up from the
// card every 10 ms until the file ends -- or, looped, forever.
//
// The ALARM is one more request on the same path: the selected tone, looped, ramped up over
// 30 s (§6.6).  When that cannot play -- no card, no tone chosen, the file gone or wrong, the
// card pulled mid-ring -- it falls back to a generated two-note beep, because an alarm that
// stays silent because of a file is the one failure this product may not have.
//
// UPLOADS (app/PROTOCOL.md "Sound files"): `put_begin` opens `/sd/tones/.<name>.part`, the
// phone streams the bytes through the `blob` characteristic into put_data(), `put_end` checks
// the length, the CRC-32 and the WAV header and only then renames the file into place -- so a
// half-sent or wrong file is never visible to `ls`, the alarm, or the app's listing.
//
// HISTORY (§6.3a, app/PROTOCOL.md "History"; storage_history.cpp): `net` feeds a reading every
// 10 s, this AO averages them into one 24-byte record per period (default 5 min) on the UTC
// clock, keeps records in a RAM ring and appends them to `/sd/log/<yyyy>/<mmdd>.bin` every
// 15 min.  Retention by age AND by megabytes, both measured on the card.  The phone downloads
// a day file through the `bulk` characteristic (`log fetch`), resumable by offset.
#pragma once

#include <cstdint>

#include "clk/ao.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/wav.hpp"
#include "clk/transport/history.hpp"
#include "clk/transport/snapshot.hpp"

namespace clk::svc {

class Storage final : public ActiveObject {
public:
    static constexpr uint32_t kAlarmRampMs = 30000;  // §6.6: "tone ramps 30 s"
    static constexpr std::size_t kNameMax = hal::sd::kNameMax;

    enum class Playing : uint8_t { Nothing, File, Beep };

    struct Snapshot {
        bool mounted;
        hal::sd::Info card;         // valid when mounted
        char alarm_tone[kNameMax];  // the selection; "" = none chosen (the beep)
        Playing playing;
        bool alarm;  // what is playing is the alarm
        bool loop;
        char file[kNameMax];  // bare name under /sd/tones, or the path given
        uint32_t pos_bytes;   // of the data chunk, read from the card so far this pass
        uint32_t data_bytes;
        uint32_t loops;      // completed passes
        uint32_t underruns;  // from the mixer: the ring ran dry
        // The upload, if one is open.
        bool put_open;
        bool put_failed;  // the card refused a write: only `put abort` or a new begin now
        char put_name[kNameMax];
        uint32_t put_size;
        uint32_t put_next;  // the offset the next blob write must carry
        // The answer to the last request, for whoever queued it.
        uint32_t done_seq;  // the last request finished -- compare with a returned seq
        Status last_st;
        const char* last_why;    // a string literal, or nullptr
        hal::wav::Err last_wav;  // Ok unless a header was refused
    };

    Storage() noexcept;

    // Queue a request; the return value is its sequence number, and the request is answered
    // once snapshot().done_seq has reached it.  0 = the queue was full (nothing queued).
    //
    // `name` is a bare file name under /sd/tones ("birds.wav"), or a full /sd/... path.
    uint32_t play(const char* name, bool loop) noexcept;
    uint32_t ring_alarm() noexcept;  // the selected tone, looped + ramped; the beep otherwise
    uint32_t stop() noexcept;        // whatever is playing, file or beep, with a fade
    // Delete a file from /sd/tones (bare name).  Stops it first if it is playing; clears the
    // alarm selection -- back to the beep -- if it was the alarm tone.
    uint32_t remove(const char* name) noexcept;

    // Uploads.  `crc` is CRC-32/ISO-HDLC (zlib's crc32) of the whole file.  A begin with the
    // same name, size and CRC as the open upload RESUMES it: snapshot().put_next says where.
    static constexpr uint32_t kMaxUpload = 16u * 1024u * 1024u;  // ~2.9 min at 48 kHz mono
    uint32_t put_begin(const char* name, uint32_t size, uint32_t crc) noexcept;
    uint32_t put_end() noexcept;
    uint32_t put_abort() noexcept;
    // One blob write: 4-byte LE offset, then data.  ANY thread -- it is called on the BLE host
    // task -- and it only copies: Ok (accepted), Busy (queue full, retry), BadArg (wrong
    // offset or past the size), NotReady (no upload open), Failed (the card refused earlier).
    Status put_data(const uint8_t* blob, std::size_t len) noexcept;

    static uint32_t crc32(uint32_t crc, const uint8_t* p, std::size_t n) noexcept;
    // A name an upload may create: bare, ends in .wav, no characters FAT refuses, no leading dot.
    static bool valid_upload_name(const char* name) noexcept;
    uint32_t mount() noexcept;
    uint32_t unmount() noexcept;
    // Choose the alarm tone: a bare name under /sd/tones, checked on the card (it must open
    // and pass the header check) and then persisted to NVS.  "" clears it -- the beep.
    uint32_t select_tone(const char* name) noexcept;

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // ---- history (storage_history.cpp) ----
    struct LogCfg {
        uint16_t period_s = 300;  // one record per this many seconds
        uint16_t keep_days = 731;
        uint16_t cap_mb = 200;  // on-card bytes, cluster-rounded, all day files together
        bool on = true;
    };
    static constexpr uint16_t kPeriodMin = 10, kPeriodMax = 3600;
    static constexpr uint16_t kKeepMin = 1, kKeepMax = 3650;
    static constexpr uint16_t kCapMin = 10, kCapMax = 2000;
    struct LogSnap {
        LogCfg cfg;
        bool time_ok;  // records are only made once the clock has a real date
        uint32_t ram;  // records waiting in RAM for the card
        uint32_t ram_cap;
        uint32_t written;  // records appended to the card since boot
        uint32_t dropped;  // records lost: RAM full with no card
        uint32_t flushes;
        uint32_t flush_ago_s;     // since the last write to the card (UINT32_MAX = never)
        uint32_t cluster;         // the card's, or the 32 KB assumed without one
        uint64_t used_bytes;      // on the card, cluster-rounded, as of the last prune
        uint32_t days;            // day files on the card, as of the last prune
        uint32_t oldest, newest;  // yyyymmdd, 0 = none
        const char* last_err;     // a literal, or nullptr
        // The download in progress, and the answer to the last `log fetch`.
        bool fetching;
        uint32_t fetch_day;  // yyyymmdd
        uint32_t fetch_from, fetch_size, fetch_crc, fetch_sent;
    };
    // `net`'s reading, every 10 s, any thread.  Only the room / light / battery / radio parts
    // of the snapshot are used.
    void log_feed(transport::Snapshot const&) noexcept;
    // Something happened.  Any thread; stamped now (back-dated if the time is not known yet).
    void log_event(transport::hist::Ev, const uint8_t* args = nullptr, std::size_t n = 0) noexcept;
    // Denied when the projection (period x keep, cluster-rounded) exceeds the cap -- `why`
    // then holds the numbers.  BadArg out of range.  Persisted (NVS).
    Status log_set(LogCfg const&, char* why, std::size_t cap) noexcept;
    [[nodiscard]] LogCfg log_cfg() const noexcept;
    [[nodiscard]] LogSnap log_snapshot() const noexcept;
    [[nodiscard]] static uint64_t log_projected(LogCfg const&, uint32_t cluster) noexcept;
    uint32_t log_flush() noexcept;  // write the RAM records to the card now (a request)
    // Stream [offset, size) of a day file on `bulk`.  Flushes first, so today is complete.
    // Answered with fetch_size / fetch_crc (CRC-32 of exactly those bytes) in log_snapshot().
    uint32_t log_fetch(uint32_t yyyymmdd, uint32_t offset) noexcept;
    uint32_t log_fetch_stop() noexcept;
    void set_fw_id(uint32_t id) noexcept { fw_id_ = id; }
    // The last few records (RAM, newest last), for `log tail`.  Returns how many were copied.
    std::size_t log_tail(uint8_t (*out)[transport::hist::kRecord], std::size_t max) const noexcept;

    // Blocking: open `path`, read and check its header.  For `storage ls`, which is a bench
    // listing and reads each file's first 512 bytes on the CLI thread (FATFS is re-entrant).
    // Everything that PLAYS goes through the queue above.
    static hal::wav::Info probe(const char* path, Status& st) noexcept;
    // "birds.wav" -> "/sd/tones/birds.wav"; a /sd/... path is taken as is.  False when it
    // cannot be a path at all (empty, too long, outside /sd).
    static bool resolve(const char* name, char* out, std::size_t cap) noexcept;

protected:
    void on_start() override;
    void on_event(Event const&) override;
    void on_tick() override;

private:
    enum class Kind : uint8_t {
        Mount,
        Unmount,
        Play,
        Alarm,
        Stop,
        Select,
        Remove,
        PutBegin,
        PutEnd,
        PutAbort,
        LogFlush,
        LogFetch,
        LogFetchStop,
    };
    struct Req {
        Kind kind;
        bool loop;
        char name[kNameMax];
        uint32_t seq;
        uint32_t size, crc;  // PutBegin
    };
    // Upload data in flight between the BLE host task and this AO.  Four is plenty: the phone
    // waits for each write's response, so more than one or two only queue while a card write
    // is slow -- and then Busy is the right answer.
    struct Chunk {
        uint16_t len;
        uint8_t data[hal::ble::kMaxBlob];
    };
    static constexpr std::size_t kChunks = 4;
    static constexpr std::size_t kQueue = 4;

    uint32_t enqueue(Kind, const char* name, bool loop, uint32_t size = 0,
                     uint32_t crc = 0) noexcept;
    void drain_chunks() noexcept;
    void put_close(bool discard) noexcept;
    void put_path(char* out, std::size_t cap) const noexcept;
    void handle(Req const&) noexcept;
    void answer(uint32_t seq, Status, const char* why, hal::wav::Err = hal::wav::Err::Ok) noexcept;
    Status ensure_mounted() noexcept;
    Status start_file(const char* name, bool loop, bool alarm, uint32_t ramp_ms, const char** why,
                      hal::wav::Err* werr) noexcept;
    void start_beep() noexcept;
    void pump() noexcept;  // top the ring up from the card
    void beep_tick() noexcept;
    void close_file() noexcept;
    void halt(bool fade) noexcept;  // stop whatever is playing
    void publish() noexcept;
    void retick() noexcept;  // 10 ms while playing or downloading, else idle
    // history
    void log_start() noexcept;
    void log_tick() noexcept;
    void log_emit(uint32_t t) noexcept;
    void log_push(const uint8_t* rec) noexcept;  // hmx_ held
    Status log_write() noexcept;                 // RAM -> card
    void log_prune(uint32_t today) noexcept;
    Status log_fetch_begin(uint32_t day, uint32_t off) noexcept;
    void log_fetch_pump() noexcept;
    void log_fetch_end(const char* why) noexcept;

    mutable port::Mutex mx_;  // guards snap_ and the request queue
    Snapshot snap_{};
    Req q_[kQueue]{};
    std::size_t q_n_ = 0;
    uint32_t next_seq_ = 1;
    // Upload, shared with put_data() -- under mx_.
    Chunk chunks_[kChunks]{};
    std::size_t chunk_head_ = 0, chunk_n_ = 0;
    bool put_open_ = false;
    bool put_failed_ = false;
    uint32_t put_next_ = 0;  // accepted (queued) so far
    uint32_t put_size_ = 0;

    // --- this AO's thread only ---
    bool mounted_ = false;
    hal::sd::Info card_{};
    char tone_[kNameMax] = {};
    Playing playing_ = Playing::Nothing;
    bool alarm_ = false;
    bool loop_ = false;
    char file_[kNameMax] = {};
    int fd_ = -1;
    hal::wav::Info wav_{};
    uint32_t left_ = 0;  // data bytes still to read this pass
    uint32_t loops_ = 0;
    uint32_t stream_seq_ = 0;  // hal::audio's, as of our stream_open()
    bool eof_sent_ = false;
    uint64_t beep_t0_us_ = 0;
    uint32_t beep_step_ = 0;
    // Upload, this AO's thread only.
    int put_fd_ = -1;
    char put_name_[kNameMax] = {};
    uint32_t put_crc_want_ = 0;
    uint32_t put_crc_ = 0;  // running, over what has been written
    uint32_t put_written_ = 0;
    // 8 KB: one card read.  Internal RAM -- the SPI DMA reads into it directly.
    int16_t buf_[4096];

    // --- history.  hmx_ guards the accumulator, the ring, the pending events and hsnap_;
    // the card side is this AO's thread only.
    mutable port::Mutex hmx_;
    LogCfg cfg_{};
    LogSnap hsnap_{};
    struct Acc {
        uint32_t n = 0, n_env = 0, n_als = 0, n_pow = 0;
        int64_t temp = 0, rh = 0, press = 0;
        double lgas = 0, lux = 0;
        float lux_max = 0;
        uint64_t vbat = 0;
        uint16_t sticky = 0;  // flags that are "any time in the period"
        transport::Snapshot last{};
    } acc_{};
    uint8_t (*ring_)[transport::hist::kRecord] = nullptr;  // heap (PSRAM on target)
    std::size_t ring_cap_ = 0, ring_head_ = 0, ring_n_ = 0;
    struct Pending {
        uint64_t mono_us;
        transport::hist::Event e;
    };
    static constexpr std::size_t kPendingMax = 16;
    Pending pend_[kPendingMax]{};
    std::size_t pend_n_ = 0;
    uint32_t bucket_ = 0;  // start of the period being averaged, 0 = none yet
    uint64_t flush_at_us_ = 0;
    uint64_t last_flush_us_ = 0;  // hmx_
    uint32_t pruned_day_ = 0;     // yyyymmdd of the last prune
    bool prune_due_ = false;      // hmx_: the settings changed -- prune at the next write
    uint32_t fw_id_ = 0;
    // download, this AO's thread
    int fetch_fd_ = -1;
    uint32_t fetch_off_ = 0, fetch_end_ = 0;
    uint8_t fetch_pkt_[4 + 512] = {};
    std::size_t fetch_pkt_n_ = 0;  // a packet read but not yet accepted by the radio
};

Storage& storage() noexcept;

}  // namespace clk::svc
