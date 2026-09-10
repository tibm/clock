// TSL2591 ambient light.  One copy, both backends.        [FIRMWARE.md §6.5, §11.2, R-BOARD-4]
#include "clk/hal/tsl2591.hpp"

#include "clk/board.hpp"
#include "clk/log.hpp"

namespace clk::hal::tsl2591 {
namespace {

// Every access to this part goes through the COMMAND byte, and it is the single most common
// way a TSL2591 appears dead: bit 7 CMD must be 1, bits 6:5 TRANSACTION must be 01 (normal
// operation -- 00 and 10 are reserved), bits 4:0 the register.  So the register address on
// the wire is 0xA0 | reg, never reg.  (Datasheet p.12, COMMAND register.)
constexpr uint8_t kCmd = 0xA0;

enum Reg : uint8_t {
    ENABLE = 0x00,
    CONFIG = 0x01,
    PERSIST = 0x0C,
    ID = 0x12,
    STATUS = 0x13,
    C0DATAL = 0x14,  // C0DATAL/H, C1DATAL/H are 0x14..0x17 and read as one block
};

constexpr uint8_t kDeviceId = 0x50;  // ID register, datasheet p.16

// ENABLE bits (p.12).  AIEN/NPIEN stay off: the interrupt pin is wired (J7.6 -> expander
// GPB3) and read back below, but nothing arms a threshold yet -- `als` is a polled reading
// until the `board` AO exists, and an armed interrupt with no consumer is an expander
// interrupt storm looking for somewhere to happen.
constexpr uint8_t kPon = 0x01;
constexpr uint8_t kAen = 0x02;

// The datasheet's own ordering, so the register field value IS the index into these.
// Gain: p.6 "Gain scaling, relative to 1x gain setting"; time: p.13 ATIME.
constexpr uint16_t kGainX[] = {1, 25, 428, 9876};
constexpr uint16_t kIntegMs[] = {100, 200, 300, 400, 500, 600};
constexpr uint8_t kGainCount = 4;

// Integration time is FIXED at 100 ms and only gain is auto-ranged.  Two reasons, and the
// second is the one that matters: the gain ladder alone spans 1x -> 9876x on top of a 16-bit
// ADC, which is about seven decades and covers everything between a dark bedroom and a
// sunlit window; and every integration-time change costs a full period of latency on a call
// that already blocks.  Longer times buy low-light SNR, so this is a one-line change if a
// dark room ever proves noisy -- kIntegMs is indexed, not inlined, for exactly that.
constexpr uint8_t kIntegIdx = 0;

// Where the auto-range wants to sit, as a fraction of full scale.  Above the high mark the
// next lamp that walks past clips; below the low mark most of the ADC's bits are unused and
// the lux fit is being computed out of noise.  Deliberately wide -- a bedside clock's light
// level changes slowly, and a narrow band would re-range (and pay an integration period)
// every time a cloud moved.
constexpr float kHighFrac = 0.85f;
constexpr float kLowFrac = 0.02f;
constexpr int kMaxRerange = 4;  // bounded: read() must never be able to loop

// ams DN40's single-coefficient lux fit.  It is NOT in this datasheet -- the datasheet gives
// the two channels' spectral responsivity and stops -- so the number below is the vendor
// application note's, and it is a first-order fit rather than a calibration: it assumes the
// part is looking at broadly white light through no glass.  This clock puts it behind a
// dial aperture, so expect a scale error and trim it here against a reference meter when
// the enclosure exists.  Nothing in the product branches on absolute lux today.
constexpr float kLuxDf = 408.0f;

bool g_ready = false;
uint8_t g_gain = 1;  // start at 25x: one rung up from the bottom, so an ordinary lit room
                     // lands in band on the first read and costs no re-range

Status write(uint8_t reg, uint8_t val) noexcept {
    return i2c::write_reg(kAddr, static_cast<uint8_t>(kCmd | reg), val);
}
Result<uint8_t> read8(uint8_t reg) noexcept {
    return i2c::read_reg(kAddr, static_cast<uint8_t>(kCmd | reg));
}

// AEN off, new gain, AEN on.  The bracketing is not ceremony: CONFIG written mid-integration
// leaves the in-flight conversion half-taken at the old gain and STATUS.AVALID still set
// from the previous cycle, so the very next read returns a number that looks entirely
// plausible and was measured at a gain nobody asked for.  Dropping AEN clears AVALID, which
// turns "is this reading mine?" into a bit we can wait on instead of a delay we guessed.
Status apply_gain(uint8_t idx) noexcept {
    Status st = write(ENABLE, kPon);
    if (st == Status::Ok) st = write(CONFIG, static_cast<uint8_t>((idx << 4) | kIntegIdx));
    if (st == Status::Ok) st = write(ENABLE, static_cast<uint8_t>(kPon | kAen));
    if (st == Status::Ok) g_gain = idx;
    return st;
}

// Wait for the chip to say its integration finished, not for our own arithmetic about its
// oscillator to say so.  The timeout is generous because the datasheet's 100 ms nominal is
// specified 95-108 ms (p.6) and the internal oscillator has no better reference than that.
Status wait_valid() noexcept {
    const uint32_t period = kIntegMs[kIntegIdx];
    const uint32_t deadline = clock_::millis() + period * 2 + 20;
    for (;;) {
        const auto s = read8(STATUS);
        if (!s.ok()) return s.st;
        if (s.v & 0x01) return Status::Ok;  // AVALID
        if (clock_::expired(clock_::millis(), deadline)) return Status::NotReady;
        clock_::sleep_ms(5);
    }
}

// Both channels in ONE transaction, off the device's own address auto-increment.  Four
// separate byte reads would straddle an integration boundary sooner or later and hand back a
// ch0 and a ch1 from different measurements -- which does not look like a bug, it looks like
// a lamp flickering.
Status sample(uint16_t& ch0, uint16_t& ch1) noexcept {
    uint8_t b[4]{};
    const Status st = i2c::read_regs(kAddr, static_cast<uint8_t>(kCmd | C0DATAL), b, sizeof b);
    if (st != Status::Ok) return st;
    ch0 = static_cast<uint16_t>(b[0] | (b[1] << 8));  // low byte first (C0DATAL at 0x14)
    ch1 = static_cast<uint16_t>(b[2] | (b[3] << 8));
    return Status::Ok;
}

}  // namespace

uint16_t full_scale(uint16_t integ_ms) noexcept { return integ_ms <= 100 ? 37888 : 65535; }

float lux_from_counts(uint16_t ch0, uint16_t ch1, uint16_t gain_x, uint16_t integ_ms) noexcept {
    if (gain_x == 0 || integ_ms == 0) return -1.0f;
    const uint16_t fs = full_scale(integ_ms);
    // Saturated: the true level is somewhere above this and any number would be a guess.
    if (ch0 >= fs || ch1 >= fs) return -1.0f;
    if (ch0 == 0) return 0.0f;  // dark is a reading, not a failure to fit
    // More infrared than full-spectrum is physically impossible; it means optics or wiring,
    // and the raw channels beside this on the CLI line are what diagnose it.
    if (ch1 >= ch0) return 0.0f;

    const float cpl = (static_cast<float>(integ_ms) * static_cast<float>(gain_x)) / kLuxDf;
    const float c0 = static_cast<float>(ch0);
    const float c1 = static_cast<float>(ch1);
    const float lux = ((c0 - c1) * (1.0f - c1 / c0)) / cpl;
    return lux < 0.0f ? 0.0f : lux;
}

Status init() noexcept {
    if (g_ready) return Status::Ok;
    if (!board::present(board::Dev::Als)) return Status::NotPresent;

    // Identify before configuring.  On this bus an unplugged daughterboard NACKs and answers
    // NotPresent all by itself, so what this catches is the other case: something answering
    // at 0x29 that is not a TSL2591 -- which on a board with a false-ACKing probe (§12.0.4)
    // is a real possibility and costs an afternoon if it is discovered later.
    const auto id = read8(ID);
    if (!id.ok()) return id.st;
    if (id.v != kDeviceId) {
        CLK_LOGW(drv_als, "0x%02X answers, ID reads 0x%02X, expected 0x%02X", kAddr, id.v,
                 kDeviceId);
        return Status::Failed;
    }

    if (const Status st = write(PERSIST, 0x00); st != Status::Ok) return st;
    if (const Status st = apply_gain(g_gain); st != Status::Ok) return st;
    g_ready = true;
    CLK_LOGI(drv_als, "TSL2591 at 0x%02X: id 0x%02X, %ux gain, %u ms", kAddr, id.v, kGainX[g_gain],
             kIntegMs[kIntegIdx]);
    return Status::Ok;
}

Result<als::State> read() noexcept {
    if (const Status st = init(); st != Status::Ok) return Result<als::State>::bad(st);

    const uint16_t integ = kIntegMs[kIntegIdx];
    const uint16_t fs = full_scale(integ);
    const auto high = static_cast<uint16_t>(static_cast<float>(fs) * kHighFrac);
    const auto low = static_cast<uint16_t>(static_cast<float>(fs) * kLowFrac);

    uint16_t ch0 = 0, ch1 = 0;
    for (int attempt = 0;; ++attempt) {
        if (const Status st = wait_valid(); st != Status::Ok) return Result<als::State>::bad(st);
        if (const Status st = sample(ch0, ch1); st != Status::Ok) {
            return Result<als::State>::bad(st);
        }
        if (attempt >= kMaxRerange) break;  // the ladder ran out, or the light is moving

        // Range on ch0 alone.  ch1 is a fraction of it for any real illuminant, so a ch0 in
        // band puts ch1 in band too, and ranging on the larger channel is what keeps the
        // decision from oscillating when the infrared content changes.
        const bool too_bright = ch0 >= high || ch1 >= high;
        const bool too_dim = ch0 < low;
        uint8_t want = g_gain;
        if (too_bright && g_gain > 0) {
            --want;
        } else if (too_dim && g_gain + 1 < kGainCount) {
            ++want;
        }
        if (want == g_gain) break;  // in band, or already at the end of the ladder
        if (const Status st = apply_gain(want); st != Status::Ok) {
            return Result<als::State>::bad(st);
        }
    }

    als::State s{};
    s.ch0 = ch0;
    s.ch1 = ch1;
    s.gain_x = kGainX[g_gain];
    s.integ_ms = integ;
    s.saturated = ch0 >= fs || ch1 >= fs;
    s.lux = lux_from_counts(ch0, ch1, s.gain_x, integ);

    // The INT pin is open-drain and asserts LOW, and it does not come back to the MCU at all
    // -- it lands on expander GPB3 (R-BOARD-4).  An expander that is not answering is a
    // perfectly ordinary bench state and must not cost us the light reading we already have,
    // so a failure here is reported as "not asserted" rather than propagated.
    const auto pin = expander::get(expander::Sig::AlsInt);
    s.int_asserted = pin.ok() && !pin.v;
    return Result<als::State>::good(s);
}

void forget() noexcept {
    g_ready = false;
    g_gain = 1;
}

}  // namespace clk::hal::tsl2591
