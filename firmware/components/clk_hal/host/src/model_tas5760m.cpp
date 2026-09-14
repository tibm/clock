// TAS5760M register model, behind the fake hal::i2c.               [FIRMWARE.md §11.2, §13.9]
//
// The amp earns a model for the same reason the expander does: it is a MAIN-board part with a
// driver written against it, and what that driver can get wrong is register-shaped.  PBTL is
// one bit and BTL-by-accident puts both halves of the bridge across the speaker; A_GAIN is
// two bits in the middle of a byte whose LSB is reserved-and-must-stay-1; the volume ladder
// has an off-by-0x07 mute floor.  All three are silent on a bench and loud here.
//
// Modelled: the sixteen-byte control port with its auto-incrementing address pointer, the
// POR values from Table 8, and the fault register's latching bits.  NOT modelled: the audio
// path.  There is no PWM, no speaker and no sound -- the firmware cannot tell, and §13.9's
// rule is to model what the firmware branches on and nothing else.
#include "clk/hal/host/models.hpp"

namespace clk::hal::host::model {
namespace {

// Table 8's defaults.  Written out rather than computed so a divergence from the datasheet is
// a diff on this line.  0x07 and 0x09..0x0F are reserved and read back as whatever was last
// written, which is what the real part does with them.
constexpr uint8_t kRegCount = 0x12;
constexpr uint8_t kPor[kRegCount] = {
    0x00,                                      // 0x00 device identification
    0xFD,                                      // 0x01 power control
    0x14,                                      // 0x02 digital control
    0x80,                                      // 0x03 volume configuration
    0xCF,                                      // 0x04 left volume
    0xCF,                                      // 0x05 right volume
    0x51,                                      // 0x06 analog control
    0x00,                                      // 0x07 reserved
    0x00,                                      // 0x08 fault configuration and error status
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x09..0x0F reserved
    0xFF,                                      // 0x10 digital clipper 2
    0xFC,                                      // 0x11 digital clipper 1
};

uint8_t g_reg[kRegCount]{};
bool g_por_done = false;

// The four error bits of 0x08 are READ-ONLY on the real part -- writing them must not set
// them, or a driver that read-modify-writes the OCE threshold would appear to invent faults.
constexpr uint8_t kFaultRoMask = 0x0F;

void por() noexcept {
    for (uint8_t i = 0; i < kRegCount; ++i) g_reg[i] = kPor[i];
    g_por_done = true;
}

void ensure_por() noexcept {
    if (!g_por_done) por();
}

}  // namespace

void tas5760m_reset() noexcept { por(); }

Status tas5760m(const uint8_t* w, std::size_t wn, uint8_t* r, std::size_t rn) noexcept {
    ensure_por();
    if (wn == 0) return Status::BadArg;  // the control port always wants a subaddress first
    uint8_t reg = w[0];
    if (reg >= kRegCount) return Status::BadArg;
    for (std::size_t i = 1; i < wn; ++i) {
        if (reg == 0x00) {
            // Device Identification is R, and a write to it is silently dropped rather than
            // NACKed -- which is what lets configure()'s identity read stay meaningful.
        } else if (reg == 0x08) {
            g_reg[reg] = static_cast<uint8_t>((g_reg[reg] & kFaultRoMask) |
                                              (w[i] & static_cast<uint8_t>(~kFaultRoMask)));
        } else {
            g_reg[reg] = w[i];
        }
        reg = static_cast<uint8_t>((reg + 1) % kRegCount);
    }
    for (std::size_t i = 0; i < rn; ++i) {
        r[i] = g_reg[reg];
        reg = static_cast<uint8_t>((reg + 1) % kRegCount);
    }
    return Status::Ok;
}

uint8_t tas5760m_reg(uint8_t reg) noexcept {
    ensure_por();
    return reg < kRegCount ? g_reg[reg] : 0;
}

void tas5760m_set_faults(uint8_t bits) noexcept {
    ensure_por();
    g_reg[0x08] = static_cast<uint8_t>((g_reg[0x08] & static_cast<uint8_t>(~kFaultRoMask)) |
                                       (bits & kFaultRoMask));
}

}  // namespace clk::hal::host::model
