// The HAL surface.  api/ DECLARES; hal/esp and hal/host DEFINE.   [FIRMWARE.md §2, D14]
//
// CMake picks the implementation.  There is deliberately no #ifdef here and none in any
// driver: a driver cannot discover which implementation it was linked against, and that is
// exactly what keeps clocksim honest -- a fake that the code above can detect is a fake
// that the code above will eventually special-case.
//
// Namespace-level functions rather than virtual interfaces: there is exactly one ADC, one
// pixel chain and one I2C bus in this product, so an object adds a vtable and an ownership
// question in exchange for nothing.
#pragma once

#include <cstddef>
#include <cstdint>

#include "clk/status.hpp"

namespace clk::hal {

// ---- monotonic time --------------------------------------------------------------------
// On target this is esp_timer_get_time().  On the host it is steady_clock scaled by the
// `sim warp` factor, so a 30-minute sunrise pre-roll can be watched in 30 seconds.
namespace clock_ {
uint64_t micros() noexcept;
uint32_t millis() noexcept;
void sleep_ms(uint32_t) noexcept;  // real time, never warped -- it paces the CLI
}  // namespace clock_

// ---- ADC -------------------------------------------------------------------------------
namespace adc {
enum class Ch : uint8_t {
    Vbat,  // IO1, ADC1_CH0, behind VBAT_DIV_EN -- the /2 divider is undone here
    Opto,  // IO2, ADC1_CH1, QRE1113 phototransistor
};
Result<uint16_t> read_mv(Ch) noexcept;
// Opto normalised to 0..1 against the calibrated dark/bright span; this is the number you
// actually watch while placing the index mark.
Result<float> read_opto_norm() noexcept;
}  // namespace adc

// ---- knob (PCNT + the ENC_SW GPIO) -----------------------------------------------------
namespace knob {
struct State {
    int32_t count;  // hardware quadrature, 256 counts/rev (64 CPR x4)
    int32_t delta;  // since the previous read
    bool sw;        // true = pressed (the pin is active-low; inverted here)
};
Result<State> read() noexcept;
}  // namespace knob

// ---- SK6812 chain (SPI3 + DMA on target) -----------------------------------------------
namespace pixels {
struct Rgbw {
    uint8_t r = 0, g = 0, b = 0, w = 0;
    friend constexpr bool operator==(Rgbw const&, Rgbw const&) = default;
};
inline constexpr std::size_t kCount = 7;  // chain pos 1-2 dial (on-PCB), 3-7 status (J12)

Status set(std::size_t idx, Rgbw) noexcept;
Status set_all(Rgbw) noexcept;
Status refresh() noexcept;           // pushes the frame; nothing lights before this
Rgbw get(std::size_t idx) noexcept;  // last value written
}  // namespace pixels

// ---- wake light (LEDC, 12 V COB, plugged-only) -----------------------------------------
namespace wake {
Status set(uint8_t warm_pct, uint8_t cool_pct) noexcept;
uint8_t warm() noexcept;
uint8_t cool() noexcept;
}  // namespace wake

// ---- I2C -------------------------------------------------------------------------------
namespace i2c {
// Returns how many addresses answered; fills `out` up to `cap`.
Result<std::size_t> scan(uint8_t* out, std::size_t cap) noexcept;
Result<uint8_t> read_reg(uint8_t addr, uint8_t reg) noexcept;
Status write_reg(uint8_t addr, uint8_t reg, uint8_t val) noexcept;
}  // namespace i2c

// ---- power -----------------------------------------------------------------------------
namespace power {
struct State {
    uint16_t vbat_mv;
    uint8_t soc_pct;
    bool plugged;   // PD_PG
    bool charging;  // CHRG
    bool fault;     // FAULT
};
Result<State> read() noexcept;
}  // namespace power

// Brings the fake or the real peripherals up.  Idempotent.
Status init() noexcept;

}  // namespace clk::hal
