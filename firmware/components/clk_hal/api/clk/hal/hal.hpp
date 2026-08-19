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

// ---- movement (X40.879 dual shaft, 2x TB6612) ------------------------------------------
// A velocity-controlled microstep axis with a stop target.  That is the lowest level the
// host can honestly stand on: on target the GPTimer ISR advances a Q16.16 phase accumulator,
// indexes the quarter-sine LUT and writes 8 MCPWM comparators (D5); on the host the position
// is integrated lazily in sim time and nothing runs between calls.
//
// The trapezoidal velocity profile is deliberately NOT here -- it is the part that carries
// the bugs, so it lives above the HAL in `motion` and is identical on both.  The caller
// re-issues run() every control tick with a new velocity and the same `stop_at`, which is
// what makes the landing exact whatever the tick rate.
namespace motor {
enum class Hand : uint8_t { Hour, Minute };
inline constexpr int32_t kUstepsPerRev = 17280;  // 1080 full steps x16 -- verify `motion spr`

Status enable(bool on) noexcept;  // STEP_STBY (expander GPA1); coils dead when false
bool enabled() noexcept;

// Signed velocity: + is clockwise.  Stops on reaching `stop_at`, which is an absolute
// UNWRAPPED position -- wrapping is the caller's, so "go the long way round" is expressible.
Status run(Hand, int32_t usteps_per_s, int32_t stop_at) noexcept;
Status hold(Hand) noexcept;  // stop here, coils still energized

// Redefine the current position without moving the hand -- the result of homing.  Implies
// hold(): adopting a coordinate mid-slew would leave the pending target in the old frame.
Status adopt(Hand, int32_t pos) noexcept;

struct Axis {
    int32_t pos;  // unwrapped microsteps
    int32_t vel;  // microsteps/s, signed; 0 when parked
    bool moving;
};
Axis state(Hand) noexcept;
}  // namespace motor

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

// ---- IMU (BNO085) ----------------------------------------------------------------------
// Two things, and they are not the same kind of fact.
//
// The tap counter is an EVENT: tap-to-snooze (README §12).  Monotonic and diffed by the
// caller, exactly like the PCNT knob count, because that is what an event queue drained by
// an AO actually behaves like.
//
// GRAVITY is a measurement, and it is the one thing in this product that knows which way up
// the cube is sitting: `ui` polls it and the dial re-references its 12 to it (§6.1d).  In
// DIAL AXES -- +X right across the face, +Y at the printed 12, +Z out through the glass --
// so that nothing above the HAL has to know how the sensor board was soldered.  Units are
// m/s^2 but only the ratios are read; a driver that answers in g is not wrong, just noisier
// against the dead zone.  (This is the narrow half of R14, which v0.19 retired: the cube is
// still fixed upright as a PRODUCT, and nothing here rotates a display or changes a mode.)
//
// yaw/pitch/roll stay for the bench and for clocksim's controls.  Nothing branches on them.
namespace imu {
struct State {
    float yaw_deg, pitch_deg, roll_deg;
    float gx, gy, gz;  // gravity, dial axes
    uint16_t taps;     // monotonic, wraps
};
Result<State> read() noexcept;
}  // namespace imu

// ---- MCP23017 expander -----------------------------------------------------------------
// Named signals rather than registers.  §11.2 wants a register-level device model behind
// hal::i2c and that is still the destination -- but the `Mcp23017` driver does not exist
// yet, and the vocabulary the rest of the system already speaks is the named pin
// (`ExpanderSet{ExpanderPin, bool}`, §5).  This moves to the driver when the driver lands.
namespace expander {
enum class Sig : uint8_t {
    SpkSd,      // GPA0 out  TAS5760M mute/shutdown
    StepStby,   // GPA1 out  both TB6612 STBY, idle-low at boot
    Boost12En,  // GPA2 out  TPS55340 12 V gate -- plugged-only
    RadioOff,   // GPA3 in   rear toggle J11; closed/low = radios off
    PdPg,       // GPB0 in   CH224K power-good
    Chrg,       // GPB1 in   LT3652 charge status
    Fault,      // GPB2 in   LT3652 fault
    AlsInt,     // GPB3 in   TSL2591 INT via J7.6
    FullchgEn,  // GPB4 out  LT3652 4.2 V full-charge FET
    VbatDivEn,  // GPB5 out  Vbat-divider disconnect FET
    SpkFault,   // GPB6 in   TAS5760M fault
    CellTest,   // GPB7 out  full-cell vs no-cell discriminator
    count
};
inline constexpr std::size_t kSigCount = static_cast<std::size_t>(Sig::count);

inline constexpr const char* kSigNames[] = {
    "spk_sd", "step_stby", "boost12_en", "radio_off", "pd_pg",     "chrg",
    "fault",  "als_int",   "fullchg_en", "vbat_div",  "spk_fault", "cell_test",
};
static_assert(sizeof(kSigNames) / sizeof(kSigNames[0]) == kSigCount);

// Inputs are read-only -- set() answers BadArg, which is the honest reply to "drive the
// rear toggle from firmware".
inline constexpr bool is_output(Sig s) noexcept {
    switch (s) {
        case Sig::SpkSd:
        case Sig::StepStby:
        case Sig::Boost12En:
        case Sig::FullchgEn:
        case Sig::VbatDivEn:
        case Sig::CellTest:
            return true;
        default:
            return false;
    }
}
inline constexpr const char* name(Sig s) noexcept {
    return static_cast<std::size_t>(s) < kSigCount ? kSigNames[static_cast<std::size_t>(s)] : "?";
}

Result<bool> get(Sig) noexcept;
Status set(Sig, bool level) noexcept;
}  // namespace expander

// ---- audio (TAS5760M + I2S) ------------------------------------------------------------
// Enough to answer "is the speaker making noise", which is all anything branches on today.
// The WAV path, the HPF/limiter chain and the pop-free amp sequencing arrive with the
// `audio` AO (§6.2) and sit on top of this.
namespace audio {
Status enable(bool on) noexcept;  // I2S clocks + amp out of shutdown
bool active() noexcept;
Status set_volume_pct(uint8_t) noexcept;
uint8_t volume_pct() noexcept;
}  // namespace audio

// ---- persistent settings ----------------------------------------------------------------
// NVS on target, a file on the host.  The little that must survive a power cut and cannot be
// derived: today the per-unit hand calibration (§6.1), tomorrow the whole of §7.5's Config.
//
// int32 only, deliberately.  Everything stored so far is a microstep count or a flag, and a
// typed surface with exactly one type is a surface with no casts in it.  A key that has never
// been written answers NotPresent -- the same D16 answer as a device that is not fitted, and
// the caller's cue to keep its compiled-in default.
namespace store {
Result<int32_t> get_i32(const char* key) noexcept;
Status set_i32(const char* key, int32_t value) noexcept;
}  // namespace store

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

// Restart the whole image.  esp_restart() on target; on the host clocksim re-execs itself,
// which is the closest a laptop gets to it -- same effect either way, so `sys reboot` is one
// command with one meaning on both.  Does not return when it works; a caller that gets a
// Status back is still running the old image.
Status reboot() noexcept;

}  // namespace clk::hal
