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

}  // namespace clk::board
