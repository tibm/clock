// TSL2591 register model, behind the fake hal::i2c.               [FIRMWARE.md §11.2, §13.9]
//
// What is modelled is what the DRIVER branches on: the COMMAND byte, ENABLE's PON/AEN,
// CONFIG's gain and integration fields, the ID register, STATUS.AVALID, and four data bytes
// that are computed from the SCENE through whatever gain the driver actually programmed.
// That last clause is the whole point -- a driver that writes the gain bits into the wrong
// field, or forgets which rung it left the part on, reads back a lux that is not the lux
// that was set, and it does so here rather than on a bench under a lamp.
//
// What is NOT modelled, deliberately: integration LATENCY.  AVALID comes up the instant AEN
// is asserted, so a test does not spend 100 ms per reading and the driver's AVALID wait is
// still the thing that gates it on hardware.  Also absent: the interrupt comparators and the
// persist filter (nothing arms them yet), the no-persist channel, and the photodiodes -- the
// scene is lux, and turning lux into photocurrent is the device's physics, not the
// firmware's decision surface.
#include "clk/hal/host/models.hpp"

namespace clk::hal::host::model {
namespace {

constexpr uint8_t kCmdBit = 0x80;
constexpr uint8_t kTransMask = 0x60;
constexpr uint8_t kTransNormal = 0x20;  // 01 -- the only one this part's driver ever uses
constexpr uint8_t kAddrMask = 0x1F;

enum Reg : uint8_t {
    ENABLE = 0x00,
    CONFIG = 0x01,
    PERSIST = 0x0C,
    PID = 0x11,
    ID = 0x12,
    STATUS = 0x13,
    C0DATAL = 0x14,
    C1DATAH = 0x17,
    kRegCount = 0x18,
};

constexpr uint16_t kGainX[] = {1, 25, 428, 9876};
constexpr uint16_t kIntegMs[] = {100, 200, 300, 400, 500, 600};

// The scene's illuminant, as one number: the datasheet's typical white-LED CH1/CH0 ratio
// (p.6, "ADC count value ratio", 0.166 typical).  A fixed ratio is the honest simplification
// -- the firmware has no way to ask what is lighting the room, and giving the model a
// spectrum would be modelling physics nobody branches on.
constexpr float kIrRatio = 0.166f;

// Counts per lux at 1x gain and 100 ms, derived from ams DN40's fit inverted for the ratio
// above: ch0 = lux * integ_ms * gain / (LUX_DF * (1 - r)^2), with LUX_DF = 408.  Written as
// the denominator so the derivation is visible and one constant carries it.
constexpr float kCountsDenom = 408.0f * (1.0f - kIrRatio) * (1.0f - kIrRatio);

float g_lux = 120.0f;
uint8_t g_reg[kRegCount]{};
// The register file has to hold its POR values before anyone touches it, and a test binary
// does not necessarily call sim::reset() before the first transaction.  A lazy guard rather
// than a static constructor: this is a leaf translation unit with no ordering guarantees
// against whoever calls it first.
bool g_por_done = false;

uint16_t full_scale(uint16_t integ_ms) noexcept { return integ_ms <= 100 ? 37888 : 65535; }

bool enabled() noexcept { return (g_reg[ENABLE] & 0x03) == 0x03; }  // PON | AEN

void recompute() noexcept {
    const uint16_t gain = kGainX[(g_reg[CONFIG] >> 4) & 0x03];
    // ATIME is three bits and only six of the eight are defined.  The real part's behaviour
    // for 110/111 is unspecified; clamping is the model's, and it keeps a driver that writes
    // a bad field out of this array's memory rather than into it.
    const uint8_t at = g_reg[CONFIG] & 0x07;
    const uint16_t integ = kIntegMs[at < 6 ? at : 5];
    const uint16_t fs = full_scale(integ);

    float ch0f = 0.0f;
    if (enabled()) {
        ch0f = g_lux * static_cast<float>(integ) * static_cast<float>(gain) / kCountsDenom;
    }
    float ch1f = ch0f * kIrRatio;
    if (ch0f > static_cast<float>(fs)) ch0f = static_cast<float>(fs);
    if (ch1f > static_cast<float>(fs)) ch1f = static_cast<float>(fs);

    const auto ch0 = static_cast<uint16_t>(ch0f);
    const auto ch1 = static_cast<uint16_t>(ch1f);
    g_reg[C0DATAL] = static_cast<uint8_t>(ch0 & 0xFF);
    g_reg[C0DATAL + 1] = static_cast<uint8_t>(ch0 >> 8);
    g_reg[C0DATAL + 2] = static_cast<uint8_t>(ch1 & 0xFF);
    g_reg[C1DATAH] = static_cast<uint8_t>(ch1 >> 8);

    // AVALID tracks AEN and nothing else -- see the header comment.  Dropping AEN clearing it
    // IS modelled, because that is the handshake the driver uses to know a reading belongs to
    // the gain it just programmed, and a model that left AVALID stuck high would let a driver
    // that skipped the bracketing pass here and fail on silicon.
    g_reg[STATUS] = enabled() ? 0x01 : 0x00;
}

void por() noexcept {
    for (auto& r : g_reg) r = 0;
    g_reg[ID] = 0x50;
    g_reg[PID] = 0x00;
    g_por_done = true;
    recompute();
}

void ensure_por() noexcept {
    if (!g_por_done) por();
}

}  // namespace

Status tsl2591(const uint8_t* w, std::size_t wn, uint8_t* r, std::size_t rn) noexcept {
    ensure_por();
    if (wn == 0) return Status::NotPresent;  // this part is never addressed without COMMAND
    // A transaction whose first byte has no CMD bit is one the chip does not recognise, and
    // it NACKs.  Modelled on purpose: forgetting the 0xA0 is the classic way this device
    // appears dead, and catching it here is worth more than every other register put together.
    if (!(w[0] & kCmdBit)) return Status::NotPresent;
    if ((w[0] & kTransMask) != kTransNormal) return Status::NotPresent;  // no SF support

    uint8_t reg = w[0] & kAddrMask;
    for (std::size_t i = 1; i < wn; ++i) {
        if (reg < kRegCount) {
            g_reg[reg] = w[i];
            if (reg == ENABLE || reg == CONFIG) recompute();
        }
        reg = static_cast<uint8_t>((reg + 1) & kAddrMask);
    }
    for (std::size_t i = 0; i < rn; ++i) {
        r[i] = reg < kRegCount ? g_reg[reg] : 0;
        reg = static_cast<uint8_t>((reg + 1) & kAddrMask);
    }
    return Status::Ok;
}

void tsl2591_reset() noexcept {
    g_lux = 120.0f;
    por();
}

void set_lux(float lux) noexcept {
    ensure_por();
    g_lux = lux < 0.0f ? 0.0f : lux;
    recompute();
}

float lux() noexcept { return g_lux; }

}  // namespace clk::hal::host::model
