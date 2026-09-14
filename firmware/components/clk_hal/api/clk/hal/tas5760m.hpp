// TAS5760M amp, at register level, over hal::i2c.        [FIRMWARE.md §6.2, power_values.md §10]
//
// Platform-independent and compiled into BOTH backends, same bargain as shared/mcp23017.cpp:
// on target it drives the real HTSSOP at 0x6C, on the host it drives the register model
// behind the fake bus, and the driver under test is the driver that ships (§11.2).
//
// It knows the CHIP.  It does not know the I2S port, the SPK_SD pin or what a percentage
// means -- those are hal::audio's, above it, because they are board wiring rather than
// silicon.  The one exception is the volume MAP: percent -> dB lives here because the dB is
// only meaningful against this chip's 0.5 dB ladder, and two definitions of "50 %" is how
// the CLI and the amp come to disagree.
#pragma once

#include "clk/hal/hal.hpp"
#include "clk/status.hpp"

namespace clk::hal::tas5760m {

// SPK_SLEEP/ADR (pin 13) is strapped to GND through a 0 R (esp32.md, power_values.md §10).
// HIGH would be 0x6D.
inline constexpr uint8_t kAddr = 0x6C;

// ---- the gain structure the board is dimensioned around ----------------------------------
// A_GAIN[3:2] = 00.  What 0 dBFS comes out as, in dBV, and therefore the number every watt
// in §6.2 is computed through: 10^(19.2/20) = 9.12 V rms at full scale.
//
// ⚠ The DIGITAL BOOST (reg 0x02 [5:4]) is part of the same sum and its POR default is +6 dB.
// §6.2's ceiling table -- "0 dBFS equals 9.12 V rms" -- is only true at +0 dB, so configure()
// writes +0 dB rather than leaving the default.  `power_values.md` §10 says "digital boost
// default", which contradicts §6.2's arithmetic; §6.2 is the one the inductor cap depends on.
inline constexpr float kAnalogGainDbv = 19.2f;
inline constexpr float kFullScaleVrms = 9.12f;  // 10^(kAnalogGainDbv/20)

// ---- the volume ladder (regs 0x04/0x05) --------------------------------------------------
// 0xCF = 0 dB, one step = 0.5 dB, 0xFF = +24 dB, and anything below 0x07 mutes.
inline constexpr uint8_t kVolReg0Db = 0xCF;
inline constexpr uint8_t kVolRegMute = 0x00;
inline constexpr float kVolMinDb = -100.0f;
inline constexpr float kVolMaxDb = 24.0f;

// Percent is AMPLITUDE, not power and not a perceptual curve: 100 % = 0 dB, 50 % = -6 dB,
// 10 % = -20 dB.  It is the one map a bench can check with a scope and arithmetic, which is
// worth more during bring-up than a fader that "feels" linear.  §6.6d's volume gauge is a UI
// scale over this and can put its own curve on top later.
float db_for_pct(uint8_t pct) noexcept;
uint8_t vol_reg_for_db(float db) noexcept;
float db_for_vol_reg(uint8_t reg) noexcept;

// The four latching error bits of reg 0x08.  All but CLKE need SPK_SD toggled to clear.
struct Faults {
    bool clk;  // non-latching: MCLK/BCLK/LRCK invalid or absent right now
    bool oc;   // over-current -- 7 A per BTL pair, ~14 A in PBTL
    bool dc;   // DC on the outputs
    bool ot;   // over-temperature
};

// Write the whole configured register set, volume included -- so there is never a moment,
// even a muted one, where the ladder holds the previous session's level.  §9.2.1.2.1 requires
// this to happen with the SPK_SD PIN low and MCLK/SCLK/LRCK already running, and hal::audio is
// what sequences that; this call just writes.  Never latched: "control port register changes
// should only occur when the device is in shutdown", so the set is rewritten on every unmute
// rather than assumed to have survived one.
Status configure(bool muted, float volume_db) noexcept;

Status set_mute(bool on) noexcept;        // reg 0x03 -- allowed live, unlike everything else
Status set_volume_db(float db) noexcept;  // ditto, per §9.2.1.2.2's carve-out
float volume_db() noexcept;               // last value written, not a read-back

Result<Faults> faults() noexcept;
Result<uint8_t> read_reg(uint8_t reg) noexcept;
Status write_reg(uint8_t reg, uint8_t val) noexcept;

// Everything configure() last wrote, for `audio status` -- so the bench can see PBTL and the
// gain without decoding a hex byte.
struct Shadow {
    bool configured;
    bool pbtl;
    bool muted;
    uint8_t vol_reg;
    uint8_t analog_ctrl;   // 0x06 as written
    uint8_t digital_ctrl;  // 0x02 as written
};
Shadow shadow() noexcept;
void forget() noexcept;  // tests, and a chip that was power-cycled underneath us

}  // namespace clk::hal::tas5760m
