// Pin map + device presence.                               [FIRMWARE.md D15, §12.0, §14]
//
// D15: the pin map is IDENTICAL on every board.  The devkit carries the same
// WROOM-1U-N8R8, so a remap would be gratuitous divergence -- and exactly the kind of
// difference that hides a real bug until the PCB arrives.  Only the set of devices that
// are actually *fitted* differs, and that is a runtime bitmask because on a breadboard it
// genuinely is runtime: you wire the encoder on Tuesday and the opto on Wednesday.
#pragma once

#include <cstdint>

namespace clk::board {

// ---- pin map (esp32.md, verbatim) ------------------------------------------------------
struct Pins {
    int vbat_sense = 1;  // ADC1_CH0
    int home_opto = 2;   // ADC1_CH1
    int neopix = 7;      // SPI3 MOSI via GPIO matrix (D4)
    int i2c_sda = 8;
    int i2c_scl = 9;
    int enc_sw = 17;        // GPIO IRQ, 5 ms debounce
    int sensor_int = 42;    // BNO085 H_INTN only
    int expander_int = 44;  // MCP23017 INTA/B mirrored
    int wake_warm = 45;     // LEDC
    int wake_cool = 46;     // LEDC
    int enc_a = 47;         // PCNT
    int enc_b = 48;         // PCNT
    // microSD on SPI2, its only device since v0.19 (esp32.md).  Through the GPIO matrix -- none
    // of these are SPI2's IOMUX pins -- which caps the bus near 26 MHz; the card runs at 20.
    int sd_sclk = 13;
    int sd_mosi = 14;
    int sd_miso = 21;
    int sd_cs = 18;  // active-low, external pull-up keeps the card idle through boot

    // Stepper coils, one TB6612 per shaft -- and named for the SHAFT, not for the hand.
    //
    // Which hand goes on which shaft is a decision, and it changed on 2026-09-13: the minute
    // hand moved to the inner pin so it reads in front, the way a normal clock does
    // (`cad/README.md`, FIRMWARE.md §6.1).  The WIRING did not move with it -- driver #1 still
    // drives the outer tube -- so naming these `step_minute`/`step_hour` made a relabelling
    // look like a rewiring job and put the hand assignment in two files at once.  It is one
    // line in motor_esp.cpp's build() now, and these say only what they are soldered to.
    //
    // The schematic's own nets are still `STEP_M_*` / `STEP_H_*` (esp32.md, kicad/gen) and are
    // misnomers as of that date; kicad/REVIEW.md carries the rename for the respin.
    //
    // Order is [A+, A-, B+, B-] because that is the order the commutation writes them in, and
    // it is NOT ascending GPIO: driver #1's B- landed on IO3 (esp32.md), which is a strap pin
    // and therefore the one that had to be an output nobody drives at boot.  STEP_STBY keeps
    // both drivers off until firmware asks.
    int step_tube[4] = {4, 5, 6, 3};     // MCPWM0, TB6612 #1, `STEP_M_*` -- the OUTER tube
    int step_pin[4] = {38, 39, 40, 41};  // MCPWM1, TB6612 #2, `STEP_H_*` -- the INNER pin.
                                         //   Also the JTAG pins, see esp32.md
};
inline constexpr Pins kPins{};

// ---- devices ---------------------------------------------------------------------------
enum class Dev : uint16_t {
    Opto,
    Vbat,
    Knob,
    Pixels,
    WakeLed,
    Expander,
    Amp,
    Als,
    Env,
    Imu,
    Sd,
    Xtal32k,
    Motor,  // the X40.879 + both TB6612s, as one fitted-or-not unit
    count
};
inline constexpr int kDevCount = static_cast<int>(Dev::count);

const char* name(Dev) noexcept;
bool parse_dev(const char* s, Dev& out) noexcept;

// Presence is initialised from the board's compile-time default and can then be changed --
// by probing on target (`board i2c scan` finding the expander), or by `sim present` on the
// host.  An absent device yields Status::NotPresent, never a faked success (D16).
bool present(Dev) noexcept;
void set_present(Dev, bool) noexcept;
void reset_presence() noexcept;  // back to the compile-time default

const char* board_name() noexcept;  // "rev0_3" | "devkit" | "host"

// Does this board start with the movement inhibited?  True on the physical boards while
// milestone 3 is open -- a soldered-through movement cannot be unplugged, and homing on boot
// drives both hands the instant the board powers up, which during sensor bring-up is a hazard
// and a nuisance.  False on the host, so clocksim and the test suite are untouched.
// ⚠ Flip this to false for the physical boards when milestone 3 closes.
bool motor_inhibited_default() noexcept;

}  // namespace clk::board
