// TSL2591 ambient-light sensor, at register level, over hal::i2c.   [FIRMWARE.md §6.5, §11.2]
//
// Sensor daughterboard, J7, address 0x29 (fixed -- the part has no address strap).  Compiled
// into BOTH backends: on target it drives the real chip, on the host it drives the
// register-level model behind the fake hal::i2c, so the driver under test is the one that
// ships.
//
// It answers `als::State` and not registers, because every caller above it wants lux.
#pragma once

#include "clk/hal/hal.hpp"
#include "clk/status.hpp"

namespace clk::hal::tsl2591 {

inline constexpr uint8_t kAddr = 0x29;  // fixed, no strap (esp32.md)

// Idempotent, and called for you by read().  Powers the ADC and parks the chip on the
// mid-range rung; the auto-range in read() takes it from there.
Status init() noexcept;

// One measurement.  Blocks for at most one integration period when the chip has to be
// re-ranged -- which is why this is a HAL call and not something an ISR may make.
Result<als::State> read() noexcept;

// The lux fit, exposed because it is the part worth testing exhaustively: pure, and every
// interesting input (ch1 > ch0, both zero, either channel saturated) is a number you can type
// rather than a lamp you have to go and find.
//
//   -1  saturated -- either channel is at full scale, so the true level is somewhere above
//       this and no number would be honest
//    0  dark, or ch1 > ch0.  The second is not a light level at all: more infrared than
//       full-spectrum is physically impossible, so it means optics or wiring, and the raw
//       channels printed beside it on the same line are what diagnose it
float lux_from_counts(uint16_t ch0, uint16_t ch1, uint16_t gain_x, uint16_t integ_ms) noexcept;

// Full-scale count for an integration time.  100 ms clips at **37888**, every longer one at
// 65535 (datasheet p.13, CONTROL/ATIME table, and the ALS characteristics on p.6 agree).
// Not 36863 -- that number is widespread in hobby drivers and it is not in this datasheet;
// using it would under-report saturation by a thousand counts and quietly bias the top of
// every auto-range decision.  Saturation is judged against the TIME, never against 0xFFFF.
uint16_t full_scale(uint16_t integ_ms) noexcept;

void forget() noexcept;  // drop the "already configured" latch; tests and a power-cycled chip

}  // namespace clk::hal::tsl2591
