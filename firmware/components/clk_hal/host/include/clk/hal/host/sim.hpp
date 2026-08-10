// Control surface for the fake HAL.                        [FIRMWARE.md D14, §9.5, §11.2]
//
// Host build only.  This is what the `sim` command group writes to and what the fake
// peripherals read from.  Nothing above hal/ may include this header -- if a service could
// reach it, the service could tell it was running against fakes, and D14 would be a lie.
#pragma once

#include <cstddef>
#include <cstdint>

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"

namespace clk::hal::host {

// ---- time ------------------------------------------------------------------------------
// Warp scales sim time against wall time.  Changing it re-bases, so time never jumps
// backwards and an already-scheduled deadline stays where it was in sim time.
void   set_warp(double factor) noexcept;      // 1.0 = real time; clamped to [0.01, 10000]
double warp() noexcept;
void   advance(uint64_t sim_us) noexcept;     // instantaneous jump, for `sim jump`

// ---- analog ----------------------------------------------------------------------------
void  set_opto(float norm) noexcept;          // 0 = dark, 1 = full reflection; clamped
float opto() noexcept;
void  set_vbat_mv(uint16_t mv) noexcept;
void  set_noise_mv(uint16_t peak) noexcept;   // +/- peak, deterministic PRNG
void  set_seed(uint32_t) noexcept;            // reproducible streams for tests

// ---- knob ------------------------------------------------------------------------------
void  turn(int32_t detents) noexcept;         // 4 PCNT counts per detent
void  press(uint32_t hold_ms) noexcept;       // held for hold_ms of SIM time

// ---- power -----------------------------------------------------------------------------
void  set_plugged(bool) noexcept;
bool  plugged() noexcept;

// ---- readback (for the CLI and for tests) ----------------------------------------------
// 7 chars + NUL, one per pixel, dominant-channel letter: '.' off, R G B W C M Y, 'o' other.
void  render_pixels(char* out, std::size_t cap) noexcept;
bool  refreshed() noexcept;                   // has refresh() been called since the last set?

// Restores every fake to its power-on value.  Called by `sim reset` and by each test.
void  reset() noexcept;

}  // namespace clk::hal::host
