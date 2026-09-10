// Register-level device models behind the fake hal::i2c.   [FIRMWARE.md §11.2, §13.9 item 9]
//
// This is the half of D14 that makes the host worth anything: the thing being faked is the
// HARDWARE, so the driver under test is the one that ships.  `hal::als::read()` on the host
// runs `tsl2591::read()` -- the same file the ESP32 runs -- against a register file here.
//
// Which devices get one is a judgement, and §13.9 item 9 already made it: model what the
// FIRMWARE branches on, never the device's own physics.  So the TSL2591 has a gain register,
// an integration-time register, an ADC-enable bit and a saturation ceiling, because
// auto-ranging branches on every one of them -- and it has no photodiode.  The BME688 has its
// calibration block and its status bits, because the compensation arithmetic is where the
// bugs live.  The BNO085 has NO model: an SHTP responder is a week of work to test code that
// only real silicon can invalidate, so the host keeps its angle-based `hal::imu` fake and the
// bno085 driver is target-only.  That asymmetry is deliberate and documented, not an omission.
//
// LOCKING: every function here is called with the fake HAL's bus lock already held, and none
// of them may call back into hal::.  A model keeps ordinary unsynchronised state; the lock
// upstream is what serialises it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "clk/status.hpp"

namespace clk::hal::host::model {

// One transaction, in hal::i2c::write_read()'s shape: `wn` bytes out, then `rn` bytes in,
// either half possibly empty.  A model answers Status::Ok when it handled the transaction,
// NotPresent when the transaction is one this device would NACK.  Presence -- whether the
// daughterboard is plugged in at all -- is checked by the caller, not here.
using Xfer = Status (*)(const uint8_t* w, std::size_t wn, uint8_t* r, std::size_t rn) noexcept;

// ---- TSL2591 at 0x29 -------------------------------------------------------------------
Status tsl2591(const uint8_t* w, std::size_t wn, uint8_t* r, std::size_t rn) noexcept;
void tsl2591_reset() noexcept;  // back to POR: the chip, not the scene

// The scene, not the chip.  What the room is doing, in lux -- the model turns it into counts
// through whatever gain and integration time the driver has programmed, which is exactly the
// path an auto-ranging bug takes.
void set_lux(float lux) noexcept;
float lux() noexcept;

// ---- BME688 at 0x77 --------------------------------------------------------------------
Status bme688(const uint8_t* w, std::size_t wn, uint8_t* r, std::size_t rn) noexcept;
void bme688_reset() noexcept;

// The scene again: real physical quantities, which the model runs BACKWARDS through the
// datasheet's compensation formulas into raw ADC words.  A driver that gets the arithmetic
// right reads back what was set here; one that gets it wrong reads back something else, and
// that is the entire point of modelling this device rather than returning zeros.
void set_env(float temp_c, float rh_pct, float press_hpa, uint32_t gas_ohms) noexcept;
void env_scene(float& temp_c, float& rh_pct, float& press_hpa, uint32_t& gas_ohms) noexcept;

}  // namespace clk::hal::host::model
