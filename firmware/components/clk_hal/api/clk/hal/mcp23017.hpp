// MCP23017 IO expander, at register level, over hal::i2c.        [FIRMWARE.md §6.5, §11.2]
//
// Platform-independent and compiled into BOTH backends: on target it drives the real chip at
// 0x20, on the host it drives the register-level device model behind the fake hal::i2c.  That
// is the point of §11.2 -- the thing being faked is the HARDWARE, so the driver under test is
// the one that ships.
//
// It speaks the board's vocabulary (expander::Sig), not registers, because every caller above
// it does.  The register map is an implementation detail and stays in the .cpp.
#pragma once

#include "clk/hal/hal.hpp"
#include "clk/status.hpp"

namespace clk::hal::mcp23017 {

inline constexpr uint8_t kAddr = 0x20;  // A2/A1/A0 strapped to GND (esp32.md)

// Idempotent, and called for you by get()/set().  Order is a hardware requirement, not a
// preference -- see the .cpp.
Status init() noexcept;

Result<bool> get(expander::Sig) noexcept;
Status set(expander::Sig, bool level) noexcept;  // inputs answer BadArg

// Drop the "already configured" latch.  For tests and for a chip that has been power-cycled
// underneath us; nothing in the product calls it.
void forget() noexcept;

}  // namespace clk::hal::mcp23017
