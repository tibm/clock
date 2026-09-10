// BNO085 sensor hub over SHTP/SH-2.  One copy, both backends.
// [FIRMWARE.md §6.5.1, R-BOARD-3 · datasheet 1000-3927 v1.17 §1.3, §5.2]
//
// Scope, and it is narrower than §6.5.1's destination on purpose -- see the header.  What
// this file implements is the smallest conversation that answers milestone 1's question:
// drain the boot chatter, confirm the hub with a product-ID handshake, enable exactly two
// reports, and parse them.  Everything it parses, CEVA's `sh2` parses the same way.
//
// Confidence, field by field, because the SH-2 reference manual is NOT in this repo and the
// next person will want to know which numbers came from where:
//   * from the datasheet, cited inline -- the SHTP header, the six channels, the executable
//     channel's reset-complete, the product-ID request/response, the 17-byte Set Feature
//     layout, and the timebase-plus-report shape of a channel-3 packet (Figures 1-26..1-33,
//     5-1, 5-2).
//   * CEVA's published SH-2 constants, NOT in this datasheet -- the two feature report IDs
//     (gravity 0x06, tap 0x10), gravity's Q point of 8, and the tap report's flags byte.
//     These are stable across every sh2 release and every driver built on it, but they are
//     the lines to check first if the hub answers and the numbers are nonsense.
#include "clk/hal/bno085.hpp"

#include <atomic>
#include <cmath>
#include <cstring>

#include "clk/board.hpp"
#include "clk/log.hpp"

namespace clk::hal::bno085 {
namespace {

// The SHTP header is 4 bytes and its length field COUNTS THEM (datasheet Fig. 1-26), which is
// the single most common way to desync this protocol.
constexpr std::size_t kHdr = 4;

// One cargo buffer.  128 bytes holds any report packet we care about several times over --
// a timebase plus a gravity report is 19 bytes -- while being far too small for the boot
// advertisement, which is a few hundred bytes of channel-0 application metadata.  That is
// the right trade rather than an accepted limitation: nothing here parses advertisements, so
// the oversized-packet path below exists to DISCARD one cleanly, and sizing the buffer to fit
// it would spend a quarter of a kilobyte to hold something we throw away.
constexpr std::size_t kBuf = 128;

// Report intervals.  Gravity at 500 ms is §6.1d's plugged cadence -- which way up a cube is
// sitting is a shelf, not a gesture, and asking for it faster buys nothing but I2C traffic.
// Tap is an event sensor; its interval is the FLOOR on how often it may report, so 100 ms
// bounds snooze latency well below what a finger notices without waking us needlessly.
constexpr uint32_t kGravityIntervalUs = 500'000;
constexpr uint32_t kTapIntervalUs = 100'000;

// Bounds.  Every loop in this file has one, because R-BOARD-3 means a wedged hub can only be
// observed and never reset: an unbounded retry here would hand a silent BNO085 the power to
// stall the caller and hold the bus away from the expander and the amp.
constexpr uint32_t kBootDrainMs = 600;  // §6.5.1 item 3: "budget a few hundred ms"
constexpr uint32_t kReplyWaitMs = 400;  // for the product-ID response
constexpr int kMaxDrainPackets = 16;    // per read(), so read() cannot be held open
constexpr uint32_t kReinitBackoffMs = 5000;

std::atomic<uint32_t> g_int_edges{0};

bool g_ready = false;
uint32_t g_seq[6]{};  // per channel, per direction (datasheet Fig. 1-26, SeqNum)
uint32_t g_packets = 0;
uint32_t g_errors = 0;
uint32_t g_last_ms = 0;
bool g_ever = false;         // has a gravity report ever arrived?
uint32_t g_next_try_ms = 0;  // re-init backoff, so a dead hub costs one handshake per 5 s
float g_gx = 0.0f, g_gy = 0.0f, g_gz = 0.0f;
uint16_t g_taps = 0;

// SH-2 report payload lengths, INCLUDING the report ID.  A packet on channel 3 is a run of
// these back to back (datasheet Fig. 5-2), so walking it needs a length for every ID that can
// appear -- and an unknown ID means the walk has to stop rather than guess, because guessing
// wrong turns one unrecognised report into a stream of invented ones.
constexpr std::size_t report_len(uint8_t id) noexcept {
    switch (id) {
        case kReportTimebase:
            return 5;  // 0xFB + 4-byte base delta
        case kReportGravity:
            return 10;  // id, seq, status, delay, then three int16 (Fig. 5-2's shape)
        case kReportTap:
            return 5;  // id, seq, status, delay, flags
        default:
            return 0;
    }
}

constexpr int16_t s16le(const uint8_t* p) noexcept {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8)));
}

// One SHTP cargo out.  The sequence number is per channel and per direction and must advance
// on every send, because the hub uses it to spot duplicates -- a driver that always sends 0
// works until it doesn't.
Status send(uint8_t chan, const uint8_t* body, std::size_t n) noexcept {
    if (chan >= 6 || n + kHdr > kBuf) return Status::BadArg;
    uint8_t pkt[kBuf];
    const auto len = static_cast<uint16_t>(n + kHdr);
    pkt[0] = static_cast<uint8_t>(len & 0xFF);
    pkt[1] = static_cast<uint8_t>((len >> 8) & 0xFF);
    pkt[2] = chan;
    pkt[3] = static_cast<uint8_t>(g_seq[chan]++);
    std::memcpy(pkt + kHdr, body, n);
    return i2c::write(kAddr, pkt, len);
}

// One SHTP cargo in, or "nothing pending".
//
// Two transactions, and that is the part the datasheet is explicit about (§1.3.1): read the
// 4-byte header to learn the length, then repeat the read for that many bytes -- and the
// second read RETURNS THE HEADER AGAIN.  There is no "continue from where I left off" on this
// interface; every read starts a fresh look at the same cargo.  A driver written as though
// the second read began at the payload is off by four bytes on every packet.
//
// `out` receives the PAYLOAD only.  Returns NotReady when the hub has nothing to say, which
// is the overwhelmingly common case and is not a failure.
Status recv(uint8_t* out, std::size_t cap, std::size_t& n, uint8_t& chan) noexcept {
    n = 0;
    uint8_t hdr[kHdr]{};
    if (const Status st = i2c::read(kAddr, hdr, sizeof hdr); st != Status::Ok) return st;

    Header h{};
    if (!parse_header(hdr, h)) return Status::NotReady;  // length 0: nothing waiting

    const std::size_t total = h.len + kHdr;
    uint8_t buf[kBuf];
    const std::size_t want = total < kBuf ? total : kBuf;
    if (const Status st = i2c::read(kAddr, buf, want); st != Status::Ok) return st;

    g_last_ms = clock_::millis();
    ++g_packets;

    // Too big for the buffer -- the boot advertisement, in practice.  Drain it in
    // buffer-sized bites and report nothing: the hub re-offers the remainder with an updated
    // length on each read (§1.3.1, "the length will be updated on a subsequent read"), so
    // this resynchronises rather than leaving half a cargo in front of the next parse.
    if (total > kBuf) {
        for (std::size_t left = total - want; left > 0;) {
            const std::size_t bite = left < kBuf ? left : kBuf;
            if (i2c::read(kAddr, buf, bite) != Status::Ok) break;
            left -= bite;
        }
        return Status::NotReady;
    }

    chan = h.chan;
    n = h.len;
    if (n > cap) n = cap;
    std::memcpy(out, buf + kHdr, n);
    return Status::Ok;
}

// A channel-3 packet: a timebase reference followed by one or more sensor reports, back to
// back (datasheet Fig. 5-2).  Nothing here uses the timebase -- the reports we want are a
// shelf and an event, neither of which needs the hub's microsecond clock -- but it has to be
// STEPPED OVER, and its five bytes are why.
void parse_input(const uint8_t* p, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n;) {
        const uint8_t id = p[i];
        const std::size_t len = report_len(id);
        if (len == 0 || i + len > n) {
            // An ID we have no length for.  Stopping is the only safe move: the reports are
            // packed with no delimiter, so skipping a guessed number of bytes would parse the
            // middle of one report as the start of another and fabricate taps.
            ++g_errors;
            return;
        }
        if (id == kReportGravity) {
            // Gravity is three int16 at Q8, in m/s^2, after the four-byte report preamble
            // (id, seq, status, delay).  §6.5.1 item 4: this is ALREADY fused and de-noised
            // with linear acceleration removed, so it must not be filtered again here --
            // §6.1d's dead zone is about the room moving, not about the signal.
            const float sx = from_q(s16le(p + i + 4), 8);
            const float sy = from_q(s16le(p + i + 6), 8);
            const float sz = from_q(s16le(p + i + 8), 8);
            to_dial_axes(sx, sy, sz, g_gx, g_gy, g_gz);
            g_ever = true;
        } else if (id == kReportTap) {
            // One report is one tap event; a double tap arrives as a single report with the
            // double bit set in its flags byte, so counting reports counts taps.  Monotonic
            // and never reset here -- the caller diffs it, exactly like the PCNT knob count,
            // so a missed poll costs no taps.
            ++g_taps;
        }
        i += len;
    }
}

// Read whatever is waiting, bounded.  Returns how many cargoes were consumed.
int drain(int max_packets) noexcept {
    uint8_t body[kBuf]{};
    int got = 0;
    for (int i = 0; i < max_packets; ++i) {
        std::size_t n = 0;
        uint8_t chan = 0xFF;
        const Status st = recv(body, sizeof body, n, chan);
        if (st == Status::NotReady) break;  // nothing pending, or an advertisement discarded
        if (st != Status::Ok) {
            ++g_errors;
            break;
        }
        ++got;
        if (chan == kChanInput || chan == kChanWake) parse_input(body, n);
    }
    return got;
}

Status set_feature(uint8_t feature, uint32_t interval_us) noexcept {
    // Datasheet Fig. 1-33, all seventeen bytes.  Everything we do not use is zero and stays
    // named so the next person does not have to count offsets:
    //   0     report ID 0xFD          8     report interval MSB
    //   1     feature report ID       9-12  batch interval (0 = no batching)
    //   2     feature flags (0)       13-16 sensor-specific config word (0)
    //   3-4   change sensitivity (0)
    //   5-8   report interval, microseconds, little-endian
    uint8_t body[17]{};
    body[0] = kReportSetFeature;
    body[1] = feature;
    body[5] = static_cast<uint8_t>(interval_us & 0xFF);
    body[6] = static_cast<uint8_t>((interval_us >> 8) & 0xFF);
    body[7] = static_cast<uint8_t>((interval_us >> 16) & 0xFF);
    body[8] = static_cast<uint8_t>((interval_us >> 24) & 0xFF);
    return send(kChanCtrl, body, sizeof body);
}

}  // namespace

// ---- the pure half ----------------------------------------------------------------------

bool parse_header(const uint8_t raw[4], Header& out) noexcept {
    const auto word = static_cast<uint16_t>(raw[0] | (static_cast<unsigned>(raw[1]) << 8));
    out.cont = (word & 0x8000u) != 0;
    const uint16_t total = word & 0x7FFFu;
    out.chan = raw[2];
    out.seq = raw[3];
    out.len = 0;
    // 0xFFFF is reserved precisely because a failed peripheral produces it too easily
    // (datasheet §1.3.1), so it is a fault and not a 32763-byte cargo.
    if (word == 0xFFFFu) return false;
    // The length INCLUDES the header, so anything under four bytes is not a short packet --
    // it is no packet.  Zero is the idle answer and it is by far the most common one.
    if (total < kHdr) return false;
    if (out.chan >= 6) return false;
    out.len = static_cast<uint16_t>(total - kHdr);
    return true;
}

void to_dial_axes(float sx, float sy, float sz, float& gx, float& gy, float& gz) noexcept {
    // PROVISIONAL, and identity for a reason rather than for want of a decision: the BNO085
    // reports in the Android frame (+X right, +Y towards the top of the device, +Z out of the
    // face, datasheet Fig. 2-1) and the dial frame is the same convention (+X right across
    // the face, +Y at the printed 12, +Z out through the glass).  So identity is correct
    // exactly when the daughterboard is mounted with its own axes aligned to the dial, which
    // is how it should be fitted and is not yet how it is known to be fitted -- the sensor
    // board is a separate PCB on a 152 mm harness and the enclosure CAD does not exist.
    //
    // CONFIRM IT ON THE BENCH, and correct it HERE and nowhere else (§6.5.1 item 4a):
    //   stand the clock upright, facing you   -> `sensor imu read` must show up=0
    //   lay it on its RIGHT-hand face         -> up=270
    //   lay it on its LEFT-hand face          -> up=90
    //   turn it upside down                   -> up=180
    //   dial to the ceiling                   -> tilt near 0, `up` meaningless (§6.1d's dead
    //                                            zone is what that case is for)
    // If those come out permuted or negated, the fix is a permutation of the three lines
    // below.  Nothing above the HAL may learn how this board was soldered.
    gx = sx;
    gy = sy;
    gz = sz;
}

// ---- the driver -------------------------------------------------------------------------

Status init() noexcept {
    if (g_ready) return Status::Ok;
    if (!board::present(board::Dev::Imu)) return Status::NotPresent;

    // R-BOARD-3's backoff.  A hub with no reset line that has stopped answering will not
    // start answering because we asked again 20 ms later, and retrying the whole handshake on
    // every read() would put a wedged BNO085 in charge of how often the expander and the amp
    // get the bus.  One attempt per five seconds is enough to catch a daughterboard being
    // plugged in and cheap enough to be invisible.
    const uint32_t now = clock_::millis();
    if (g_next_try_ms != 0 && !clock_::expired(now, g_next_try_ms)) return Status::NotReady;
    g_next_try_ms = now + kReinitBackoffMs;

    // Boot is asynchronous (§6.5.1 item 3).  After the power-on RC releases NRST the hub
    // emits an SHTP advertisement on channel 0 and a reset-complete on channel 1 before it
    // will accept configuration, and both have to be off the bus before the product-ID reply
    // can be recognised.  Bounded by wall time rather than by packet count, because on a cold
    // hub the first of them may not have happened yet.
    const uint32_t boot_end = now + kBootDrainMs;
    while (!clock_::expired(clock_::millis(), boot_end)) {
        if (drain(kMaxDrainPackets) == 0) clock_::sleep_ms(20);
    }

    // Then ask who is there.  This is the check R-BOARD-3 wants: a hub that ACKs its address
    // but never completes this handshake is wedged, and saying so early is the whole remedy
    // available to us.
    const uint8_t req[2] = {kReportProductIdReq, 0x00};  // datasheet Fig. 1-28
    if (const Status st = send(kChanCtrl, req, sizeof req); st != Status::Ok) return st;

    bool identified = false;
    uint8_t body[kBuf]{};
    const uint32_t reply_end = clock_::millis() + kReplyWaitMs;
    while (!identified && !clock_::expired(clock_::millis(), reply_end)) {
        std::size_t n = 0;
        uint8_t chan = 0xFF;
        const Status st = recv(body, sizeof body, n, chan);
        if (st == Status::NotReady) {
            clock_::sleep_ms(10);
            continue;
        }
        if (st != Status::Ok) return st;
        if (chan == kChanInput || chan == kChanWake) {
            parse_input(body, n);
        } else if (chan == kChanCtrl && n >= 6 && body[0] == kReportProductIdResp) {
            // Fig. 1-29: reset cause, SW major, SW minor, then part and build numbers.
            CLK_LOGI(drv_imu, "BNO085 at 0x%02X: SW %u.%u, reset cause %u", kAddr, body[2], body[3],
                     body[1]);
            identified = true;
        }
    }
    if (!identified) {
        CLK_LOGW(drv_imu, "no product-ID response (R-BOARD-3: no reset line, retry in %u ms)",
                 static_cast<unsigned>(kReinitBackoffMs));
        return Status::Failed;
    }

    // Exactly two features, and §6.5.1 item 4 is explicit about why the other fifteen stay
    // off: rotation vector, raw accel, gyro, mag, step counter, stability and
    // significant-motion each cost I2C traffic and power in a product that needs one bit
    // (a tap) and one vector (which way is up).
    Status st = set_feature(kReportGravity, kGravityIntervalUs);
    if (st == Status::Ok) st = set_feature(kReportTap, kTapIntervalUs);
    if (st != Status::Ok) return st;

    g_ready = true;
    CLK_LOGI(drv_imu, "BNO085: gravity @ %lu ms, tap detector armed",
             static_cast<unsigned long>(kGravityIntervalUs / 1000));
    return Status::Ok;
}

Result<imu::State> read() noexcept {
    if (const Status st = init(); st != Status::Ok) return Result<imu::State>::bad(st);

    drain(kMaxDrainPackets);

    // Nothing has ever arrived: (0,0,0) is not a gravity vector, and handing it up would give
    // `up_deg` an atan2 of two zeros and the dial a confident answer built on nothing.  The
    // tap counter is fine either way, but there is no honest State to build yet.
    if (!g_ever) return Result<imu::State>::bad(Status::NotReady);

    imu::State s{};
    s.gx = g_gx;
    s.gy = g_gy;
    s.gz = g_gz;
    s.taps = g_taps;

    // Gravity alone cannot give a heading, so there is no magnetic yaw here and none is
    // invented.  What it CAN give is the cube's rotation about the dial's own axis and its
    // tilt away from vertical -- and those are reported in the same sense clocksim's fake
    // uses, so a given pose reads the same number on the bench as it does on a laptop.
    //
    // The two lines are written out rather than taken from domain::up_deg(), which computes
    // the same angle negated: domain/ sits ABOVE hal/ in the dependency graph (§2) and a
    // driver that reached up into it would invert the layering to save an atan2.  (They are
    // the same function -- atan2 is odd in its first argument, so -atan2(-gx, -gy) is
    // atan2(gx, -gy) -- which is why `up` and this `yaw` will always disagree only in sign.)
    constexpr float kDeg = 57.295779513f;
    const float plane = std::sqrt(s.gx * s.gx + s.gy * s.gy);
    float yaw = std::atan2(s.gx, -s.gy) * kDeg;
    if (yaw < 0.0f) yaw += 360.0f;  // [0, 360), matching the fake and the ux plate
    s.yaw_deg = yaw;
    s.pitch_deg = -std::atan2(s.gz, plane) * kDeg;
    s.roll_deg = 0.0f;  // a third angle would be a second way of saying the first two
    return Result<imu::State>::good(s);
}

imu::Link link() noexcept {
    imu::Link l{};
    l.packets = g_packets;
    l.errors = g_errors;
    l.last_ms_ago = g_packets == 0 ? imu::kNever : (clock_::millis() - g_last_ms);
    l.ready = g_ready;
    return l;
}

void isr_tick() noexcept { g_int_edges.fetch_add(1, std::memory_order_relaxed); }

void forget() noexcept {
    g_ready = false;
    g_ever = false;
    g_packets = g_errors = g_last_ms = g_next_try_ms = 0;
    g_gx = g_gy = g_gz = 0.0f;
    g_taps = 0;
    for (auto& s : g_seq) s = 0;
}

}  // namespace clk::hal::bno085
