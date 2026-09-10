// BME688 temperature / humidity / pressure / gas, over hal::i2c.    [FIRMWARE.md §6.5, §11.2]
//
// Sensor daughterboard, J7, address 0x77 (SDO high via R10; R11 is the DNP alternate for
// 0x76).  Compiled into both backends against the register-level host model, because the
// compensation arithmetic is exactly the kind of code that is right on paper and wrong in C.
//
// NOT an IAQ index.  That is BSEC's or a baseline algorithm's (§6.5, license-gated) and it
// is a layer ABOVE this one; the driver answers what the silicon measured.
#pragma once

#include "clk/hal/hal.hpp"
#include "clk/status.hpp"

namespace clk::hal::bme688 {

inline constexpr uint8_t kAddr = 0x77;  // SDO strapped high on the sensor board

// Idempotent.  Reads the calibration block once (it never changes) and programs oversampling,
// the IIR filter and the gas heater profile.
Status init() noexcept;

// One FORCED measurement: wake, convert, sleep.  Blocks for the measurement duration --
// oversampling plus the heater's soak, which is why `max_hz` for this row is 1.
Result<env::State> read() noexcept;

void forget() noexcept;

}  // namespace clk::hal::bme688
