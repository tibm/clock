// TAS5760M driver.  One copy, both backends.                            [FIRMWARE.md §6.2]
#include "clk/hal/tas5760m.hpp"

#include <cmath>

#include "clk/board.hpp"
#include "clk/log.hpp"

namespace clk::hal::tas5760m {
namespace {

enum Reg : uint8_t {
    DEVICE_ID = 0x00,
    POWER_CTRL = 0x01,
    DIGITAL_CTRL = 0x02,
    VOL_CFG = 0x03,
    VOL_LEFT = 0x04,
    VOL_RIGHT = 0x05,
    ANALOG_CTRL = 0x06,
    FAULT_CFG = 0x08,
};

// ---- reg 0x01, Power Control -------------------------------------------------------------
// DigClipLev[19:14] stays at its POR 0x3F: the digital clipper is left wide open on purpose.
// The datasheet gives no numeric dBFS mapping for the 20-bit level (§8.3.2.4 is a block
// diagram and a scope trace), and the thing that actually protects L5/L6 is the FIRMWARE
// limiter at -4.1 dBFS (§6.2) -- which does not exist yet.  A guessed clip point would look
// like protection and not be it.  ⚠ Revisit when the limiter lands.
constexpr uint8_t kPowerCtrl = 0xFC | 0x01;  // clipper open, SPK_SLEEP = 0, SPK_SD bit = 1

// ---- reg 0x02, Digital Control -----------------------------------------------------------
//   [7]   HPF bypass   = 0   the chip's own DC blocker stays IN.  It is not the ~150 Hz
//                            Linkwitz-Riley that protects the DMA58-4 -- that one is the
//                            firmware biquad (§6.2) -- but a DC offset into a 4 Ohm voice
//                            coil is worth removing before there is anything else.
//   [5:4] digital boost= 00  +0 dB.  See kAnalogGainDbv: §6.2's watt table assumes it.
//   [3]   SS/DS        = 0   single speed; 48 kHz is a single-speed rate (Table 6)
//   [2:0] format       = 100 I2S, which is what i2s_std's Philips config sends
constexpr uint8_t kDigitalCtrl = 0x04;

// ---- reg 0x03, Volume Control Configuration ----------------------------------------------
// Fade = 1: the chip ramps between volume settings instead of stepping, which is the whole
// reason a live volume change is legal at all (§9.2.1.2.2's carve-out).
constexpr uint8_t kVolCfgUnmuted = 0x80;
constexpr uint8_t kVolCfgMuted = 0x83;  // + Mute R + Mute L

// ---- reg 0x06, Analog Control ------------------------------------------------------------
//   [7]   PBTL enable  = 1   OUTA+||OUTB+ and OUTA-||OUTB- into the one 4 Ohm DMA58-4
//   [6:4] PWM rate     = 101 16 x LRCK = 768 kHz at 48 kHz, the POR default; the LC filter
//                            (10 uH + 0.68 uF, f_c ~30-40 kHz) is dimensioned for it
//   [3:2] A_GAIN       = 00  19.2 dBV
//   [1]   PBTL Ch Sel  = 0   the RIGHT slot of the stereo stream feeds the bridge.  The
//                            generator writes both slots identically, so this is a
//                            documentation choice rather than a routing one -- but it has to
//                            be A choice, and the datasheet's default is the one to keep.
//   [0]   reserved     = 1   "must not be changed from its default"
constexpr uint8_t kAnalogCtrl = 0x80 | 0x50 | 0x00 | 0x00 | 0x01;
static_assert(kAnalogCtrl == 0xD1);

// Fault status bits in reg 0x08.
constexpr uint8_t kFaultClk = 1u << 3;
constexpr uint8_t kFaultOc = 1u << 2;
constexpr uint8_t kFaultDc = 1u << 1;
constexpr uint8_t kFaultOt = 1u << 0;

Shadow g_sh{};
float g_vol_db = 0.0f;

}  // namespace

float db_for_pct(uint8_t pct) noexcept {
    if (pct == 0) return kVolMinDb;
    const uint8_t p = pct > 100 ? 100 : pct;
    return 20.0f * std::log10(static_cast<float>(p) / 100.0f);
}

uint8_t vol_reg_for_db(float db) noexcept {
    if (db <= kVolMinDb) return kVolRegMute;
    const float d = db > kVolMaxDb ? kVolMaxDb : db;
    const int steps = static_cast<int>(std::lround(d * 2.0f));
    const int reg = static_cast<int>(kVolReg0Db) + steps;
    // 0x07 is -100 dB and anything under it mutes, so the ladder's own floor and our mute
    // value are the same answer with one byte between them.
    if (reg < 0x07) return kVolRegMute;
    return static_cast<uint8_t>(reg > 0xFF ? 0xFF : reg);
}

float db_for_vol_reg(uint8_t reg) noexcept {
    if (reg < 0x07) return kVolMinDb;
    return static_cast<float>(static_cast<int>(reg) - static_cast<int>(kVolReg0Db)) * 0.5f;
}

Result<uint8_t> read_reg(uint8_t reg) noexcept {
    if (!board::present(board::Dev::Amp)) return Result<uint8_t>::bad(Status::NotPresent);
    return i2c::read_reg(kAddr, reg);
}

Status write_reg(uint8_t reg, uint8_t val) noexcept {
    if (!board::present(board::Dev::Amp)) return Status::NotPresent;
    return i2c::write_reg(kAddr, reg, val);
}

Status configure(bool muted, float volume_db) noexcept {
    if (!board::present(board::Dev::Amp)) return Status::NotPresent;
    g_vol_db = volume_db;

    // The identification register first.  A chip that does not ACK is absent (D16), but a
    // chip that ACKs and then will not take a register is a bus fault wearing a working
    // scan -- and 0x6C is one bit away from 0x6D, the other strap.
    const auto id = i2c::read_reg(kAddr, DEVICE_ID);
    if (!id.ok()) return id.st;

    const uint8_t vol = vol_reg_for_db(volume_db);
    struct Write {
        uint8_t reg, val;
    };
    // Order matters in one place only: the mute goes in BEFORE the volume and the analog
    // configuration, so there is no window where a freshly configured PBTL bridge is
    // unmuted at whatever the volume register happened to hold.
    const Write set[] = {
        {VOL_CFG, muted ? kVolCfgMuted : kVolCfgUnmuted},
        {POWER_CTRL, kPowerCtrl},
        {DIGITAL_CTRL, kDigitalCtrl},
        {VOL_LEFT, vol},
        {VOL_RIGHT, vol},
        {ANALOG_CTRL, kAnalogCtrl},
    };
    for (auto const& w : set) {
        if (const Status st = i2c::write_reg(kAddr, w.reg, w.val); st != Status::Ok) {
            if (st != Status::NotPresent)
                CLK_LOGW(drv_amp, "reg 0x%02X write: %s", w.reg, clk::name(st));
            return st;
        }
    }

    // Read one back for the same reason the expander does: a write that ACKs and does not
    // stick presents later as an amp that is quiet or, worse, in BTL with both halves
    // fighting across the speaker.
    const auto back = i2c::read_reg(kAddr, ANALOG_CTRL);
    if (!back.ok()) return back.st;
    if (back.v != kAnalogCtrl) {
        CLK_LOGW(drv_amp, "0x06 reads 0x%02X, wrote 0x%02X", back.v, kAnalogCtrl);
        return Status::Failed;
    }

    g_sh.configured = true;
    g_sh.pbtl = true;
    g_sh.muted = muted;
    g_sh.vol_reg = vol;
    g_sh.analog_ctrl = kAnalogCtrl;
    g_sh.digital_ctrl = kDigitalCtrl;
    CLK_LOGI(drv_amp, "TAS5760M 0x%02X id=0x%02X: PBTL, %.1f dBV, boost +0 dB, I2S, vol %.1f dB",
             kAddr, id.v, static_cast<double>(kAnalogGainDbv), static_cast<double>(g_vol_db));
    return Status::Ok;
}

Status set_mute(bool on) noexcept {
    const Status st = write_reg(VOL_CFG, on ? kVolCfgMuted : kVolCfgUnmuted);
    if (st == Status::Ok) g_sh.muted = on;
    return st;
}

Status set_volume_db(float db) noexcept {
    const uint8_t reg = vol_reg_for_db(db);
    if (!board::present(board::Dev::Amp)) {
        // Remember it anyway.  The devkit has no amp and `audio vol 10` there should still
        // be the thing that takes effect the moment one is fitted -- D16 says absence is not
        // an error, and a setting that silently reverts is worse than one that waits.
        g_vol_db = db;
        return Status::NotPresent;
    }
    Status st = i2c::write_reg(kAddr, VOL_LEFT, reg);
    if (st == Status::Ok) st = i2c::write_reg(kAddr, VOL_RIGHT, reg);
    if (st != Status::Ok) return st;
    g_vol_db = db;
    g_sh.vol_reg = reg;
    return Status::Ok;
}

float volume_db() noexcept { return g_vol_db; }

Result<Faults> faults() noexcept {
    if (!board::present(board::Dev::Amp)) return Result<Faults>::bad(Status::NotPresent);
    const auto r = i2c::read_reg(kAddr, FAULT_CFG);
    if (!r.ok()) return Result<Faults>::bad(r.st);
    return Result<Faults>::good(Faults{(r.v & kFaultClk) != 0, (r.v & kFaultOc) != 0,
                                       (r.v & kFaultDc) != 0, (r.v & kFaultOt) != 0});
}

Shadow shadow() noexcept { return g_sh; }

void forget() noexcept {
    g_sh = Shadow{};
    g_vol_db = 0.0f;
}

}  // namespace clk::hal::tas5760m
