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

// Has `deadline` passed?  Wrap-safe, and that is the whole reason it exists: millis() rolls
// over every 49.7 days, and a plain `now >= deadline` written across that boundary either
// gives up 49 days early or waits 49 days.  A driver's I/O timeout is precisely where nobody
// would find it -- the device answers, the timeout never fires, and the one time it matters
// is a hang seven weeks after a reboot.  The unsigned difference cast to signed is the
// standard idiom and is correct for any interval under ~24.8 days.
constexpr bool expired(uint32_t now, uint32_t deadline) noexcept {
    return static_cast<int32_t>(now - deadline) >= 0;
}

// What the RTC slow clock is ACTUALLY running on, asked of the silicon -- not what the board
// was built to have.  The two differ exactly when it matters: `Y1` fails to start, IDF falls
// back to the internal RC at boot, and every holdover interval from then on drifts percent-
// level instead of ppm (§7.1, kicad/REVIEW.md #24).  A presence flag cannot see that.
enum class SlowSrc : uint8_t { RcSlow, Xtal32k, RcFastD256, Unknown };
SlowSrc slow_src() noexcept;
// Inline: the only thing that differs per platform is the register read above, and a second
// copy of this table in the other backend is a second place for the strings to drift.
constexpr const char* name(SlowSrc s) noexcept {
    switch (s) {
        case SlowSrc::Xtal32k:
            return "XTAL32K";
        case SlowSrc::RcSlow:
            return "INT_RC";
        case SlowSrc::RcFastD256:
            return "RC_FAST/256";
        default:
            return "?";
    }
}
}  // namespace clock_

// ---- ADC -------------------------------------------------------------------------------
namespace adc {
enum class Ch : uint8_t {
    Vbat,  // IO1, ADC1_CH0, behind VBAT_DIV_EN -- the /2 divider is undone here
    Opto,  // IO2, ADC1_CH1, QRE1113 phototransistor
};
// The QRE1113 reads INVERTED, and the names say so because the old ones did not.  R99 pulls
// HOME_OPTO up to +3V3 and the phototransistor pulls it down, so more reflected light is a
// LOWER voltage: nothing in front of the sensor is the top of the range, the index mark is
// down near the bottom.
//
// Measured on build #1, 2026-09-27, R99 = 22k (v0.4 V2 fitted), REAL printed hands on:
//
//     nothing ~3159 mV* | minute hand ~3142 | hour hand ~2978
//     (* at or near the 12 dB ceiling -- the true clear level may be higher)
//
// Bands, as read on the bench: > 3151 nothing, 3080-3150 minute, < 3050 hour.  The hands
// reflect far less than the 2026-09-08 reflector did (hour 2600 at R99 = 10k, i.e. ~70 uA;
// the real hour hand is ~15 uA and the minute ~7 uA), so the whole signal lives in the top
// 200 mV.  The sensor is steady enough to work there.
//
// So the span is set on the WEAK hand, not the strong one.  The hour hand is far past the
// mark and clamps to 1.
//
// And the line sits just under the CEILING, not halfway (2026-09-27, second pass): 3150 puts
// `motion`'s 0.45 `opto_thresh` at 3160 - 0.45*10 = ~3155.5 mV.  The clear level is the ADC
// clipping, not a sensor level, so it does not drift and can take a 3 mV margin; the minute
// hand DOES drift, and the first line (~3152) left it ~10 mV -- which a cold boot sometimes
// ate.  Now it has ~13.  ⚠ Only valid while clear reads >= 3156 on every sample: if
// `sensor homing stream` ever shows clear below that, this line is too close.  Homing moves ONE
// hand at a time with the other parked away, so one line that sees the weaker hand sees both; which
// hand lit it never has to be read off the level. A failed search logs the brightest reading it
// saw, in mV, next to this line.
//
// Deliberately ONE definition shared by the fake and the target: an inverted fake would have
// sent the homing FSM hunting the wrong edge on hardware, which is a mechanical-looking bug
// with a firmware cause.
inline constexpr uint16_t kOptoClearMv = 3160;  // nothing above the sensor
inline constexpr uint16_t kOptoMarkMv = 3155;   // just under clear; the minute hand is 3142

// 0 = nothing in front, 1 = fully on the mark.  Everything above the HAL is written in this
// sense (the homing FSM looks for it rising), so the inversion is undone exactly here.
inline constexpr float opto_norm_from_mv(uint16_t mv) noexcept {
    const float span = static_cast<float>(kOptoClearMv) - static_cast<float>(kOptoMarkMv);
    const float n = (static_cast<float>(kOptoClearMv) - static_cast<float>(mv)) / span;
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

Result<uint16_t> read_mv(Ch) noexcept;
// Opto normalised to 0..1 against the span above; this is the number you actually watch
// while placing the index mark.
Result<float> read_opto_norm() noexcept;
}  // namespace adc

// ---- knob (PCNT + the ENC_SW GPIO) -----------------------------------------------------
namespace knob {
struct State {
    int32_t count;  // hardware quadrature, 256 counts/rev (64 CPR x4)
    int32_t delta;  // since the previous read
    bool sw;        // true = pressed (the pin is active-low; inverted here)
    // The raw electrical level, true = the pin is LOW, before the latch and before the
    // stuck-at-boot guard.  `sw` is the answer; this is the evidence, and it is the one
    // number that separates "the driver is wrong" from "the harness is wrong" on a bench.
    bool sw_raw;
    // And the same evidence for the quadrature pair: the instantaneous level of ENC_A and
    // ENC_B, read straight off the pads that PCNT is counting.  Three raw lines beside the
    // decoded answer is what turns "the knob does not work" into a wiring diagram -- rotate
    // slowly and exactly one of {a,b} and {pin} should be moving.
    bool a_raw, b_raw;
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
// X27 spec SP-X27-e-C table row 11: gear 1:180, one electrical period = 2 deg of shaft, so
// 180 periods x 64 usteps (x16 per full step, 4 full steps per period).  Was 17280 until
// 2026-09-27, which misread the spec's 1080 PARTIAL steps (6 per period) as full steps.
inline constexpr int32_t kUstepsPerRev = 11520;  // 180 x 64 -- bench: `motion walk m 1080` = 1 turn

Status enable(bool on) noexcept;  // STEP_STBY (expander GPA1); coils dead when false
bool enabled() noexcept;

// A bench inhibit, above presence and above STEP_STBY: while it is set, enable(true) answers
// Denied and no coil is ever energised.  It exists because during bring-up the movement moving
// is a hazard and a nuisance -- homing on boot will drive both hands the moment the board comes
// up -- and "unplug it" is not available on a part that is soldered through the board.
//
// Persisted in NVS under `mot_inh`, because a switch you have to re-throw after every reset is
// a switch that will be forgotten once.  The compiled-in default is the board's (board.hpp):
// the physical boards start INHIBITED while milestone 3 is open; the host does not, so nothing
// in clocksim or the test suite changes.
Status inhibit(bool on) noexcept;
bool inhibited() noexcept;

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

// ---- bench only (2026-09-27: the bare shaft buzzes and does not turn) ----
// How the PWM off-time is spent.  Fast = IN pair L/L, which the TB6612 treats as OFF (high-Z):
// the coil current returns through the body diodes against VM, so the average coil voltage
// falls well below duty x VM once L/R approaches the 40 us carrier period.  Slow = H/H, short
// brake: the current recirculates through the low-side FETs and the average is duty x VM for
// any inductance.  Slow is the default; Fast is the scheme up to this date.
enum class Decay : uint8_t { Fast, Slow };
void set_decay(Decay) noexcept;  // takes effect on the next coil write
Decay decay() noexcept;

// Raw coil drive, bypassing the commutator: signed duty in permille of VM (-1000..1000) on
// coil A (chA, X40 contacts 1/2) and coil B (chB, 4/3).  Parks the axis and holds these
// values until the next run()/coils().  Needs enable(true).  ±1000 is DC at 5 V in either
// decay mode, so a DMM across the coil reads the bridge directly.
Status coils(Hand, int16_t a_pm, int16_t b_pm) noexcept;
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
// Two levels, and the lower one is not a convenience: `read_reg`/`write_reg` cover a chip
// whose whole conversation is one address and one byte, and NONE of the three parts on the
// sensor daughterboard is that chip.  The TSL2591 counts photons into a 16-bit pair that
// has to be read in one transaction or the two halves belong to different integrations;
// the BME688 keeps its calibration in a 30-odd byte burst; and the BNO085 is not a register
// map at all -- it is SHTP packets, header first, length in the header (S6.5.1).
//
// So the buffer forms are the primitive and the byte forms are written in terms of them.
namespace i2c {
// Returns how many addresses answered; fills `out` up to `cap`.
Result<std::size_t> scan(uint8_t* out, std::size_t cap) noexcept;

// One START, `wn` bytes out, repeated START, `rn` bytes in.  A zero length on either side
// is legal and means "skip that phase", which is how a raw read (the BNO085's header) and a
// raw write (an SHTP packet) are both expressed here.
Status write_read(uint8_t addr, const uint8_t* w, std::size_t wn, uint8_t* r,
                  std::size_t rn) noexcept;
Status write(uint8_t addr, const uint8_t* buf, std::size_t n) noexcept;
Status read(uint8_t addr, uint8_t* buf, std::size_t n) noexcept;

Result<uint8_t> read_reg(uint8_t addr, uint8_t reg) noexcept;
Status read_regs(uint8_t addr, uint8_t reg, uint8_t* out, std::size_t n) noexcept;
Status write_reg(uint8_t addr, uint8_t reg, uint8_t val) noexcept;
}  // namespace i2c

// ---- ambient light (TSL2591) -----------------------------------------------------------
// Lux is the number the product will eventually branch on -- how bright to run the dial wash
// and the status row in a dark bedroom -- so lux is what the HAL answers.  The two raw
// channels come with it because they are how you tell a real reading from a saturated one on
// the bench, and because lux is a FIT over them: full-spectrum minus infrared, which the
// datasheet's own coefficients turn into something photopic.
//
// Gain and integration time are reported, not commanded.  The driver auto-ranges (a bedside
// clock sees six decades between noon and 3 a.m.) and there is nothing above the HAL that
// wants to argue with it; seeing WHICH rung the driver settled on is what makes a suspicious
// lux value diagnosable.
namespace als {
struct State {
    float lux;          // -1 when the channels are saturated and no fit is meaningful
    uint16_t ch0;       // full spectrum (visible + IR), raw ADC
    uint16_t ch1;       // infrared, raw ADC
    uint16_t gain_x;    // 1 / 25 / 428 / 9876 -- the actual multiplier, not an enum index
    uint16_t integ_ms;  // 100..600 in 100 ms steps
    bool saturated;     // either channel at the full-scale for this integration time
    bool int_asserted;  // the TSL2591's own INT pin, read back through expander GPB3
};
Result<State> read() noexcept;
}  // namespace als

// ---- ambient / air quality (BME688) ----------------------------------------------------
// Temperature, humidity, pressure and the gas-sensor resistance -- and deliberately NOT an
// IAQ index.  An index is BSEC's (S6.5, license-gated) or a baseline algorithm's, and both
// are a layer that consumes this one.  `gas_ohms` is what the silicon measures; anything
// that turns it into a number between 0 and 500 is making a judgement, and a judgement does
// not belong under the HAL.
//
// The gas heater needs a few hundred ms at temperature and its first readings drift, so the
// two flags are part of the measurement rather than metadata: a gas resistance taken before
// `heat_stable` is a number, but not a measurement of anything.
namespace env {
struct State {
    float temp_c;
    float rh_pct;
    float press_hpa;
    uint32_t gas_ohms;
    bool gas_valid;    // the ADC completed a gas conversion in this forced measurement
    bool heat_stable;  // the heater reached its target -- until then gas_ohms drifts
};
Result<State> read() noexcept;
}  // namespace env

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

// R-BOARD-3 in one struct.  `NRST` has no host line and the board cannot cycle +3V3 either,
// so a wedged hub can be OBSERVED and never cleared -- which makes observing it the whole of
// the remedy.  Nothing above the HAL branches on this; it is what `sensor imu` prints so that
// "the clock stopped snoozing on taps" is one command away from an answer instead of a
// morning of guessing.
struct Link {
    uint32_t packets;      // SHTP packets drained since boot
    uint32_t errors;       // malformed headers, short reads, refused transactions
    uint32_t last_ms_ago;  // since the last packet; kNever when there has not been one
    bool ready;            // the hub answered a product-ID request and accepted its features
};
inline constexpr uint32_t kNever = 0xFFFF'FFFFu;
Link link() noexcept;
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
// I2S0 out (BCLK IO10, LRCLK IO11, DOUT IO12, MCLK IO43 = 256 x f_S) into the TAS5760M at
// 0x6C, PBTL mono into the 4 Ohm DMA58-4.  The chip is tas5760m.cpp's; the PIN that gates it
// (`SPK_SD`, expander GPA0), the port, the sequencing and the one signal source are here,
// because all four are board wiring rather than silicon.
//
// Two SOURCES, one at a time: the generated sine (tone(), the bring-up signal and the preview
// chime) and the stream (a WAV that `storage` pushes through a PSRAM ring, pcm.hpp).  Starting
// either one replaces the other.  The HPF/limiter chain is still to come (§6.2); the clocks,
// the register set and the sequencing under both sources are the same.
namespace audio {

// 48 kHz because the amp wants MCLK = 256 x f_S and 12.288 MHz is what IDF's default
// mclk_multiple gives at this rate (esp32.md, Table 6).  16-bit stereo frames.
inline constexpr uint32_t kRateHz = 48000;

// ⚠ THE BRING-UP VOLUME CEILING, and it is a cell-current limit, not a taste limit.
//
// With no 15 V contract the 12 V boost is off, so amp PVDD is the 5 V rail through the
// LTC4412 mux and the bridge clips at 2 x 5 V pk-pk = 3.54 V rms = 3.1 W into 4 Ohm.  §6.2's
// R-AUDIO-1 puts that case at ~1.9 A peak out of the cell -- exactly the `-GB` protector's
// 1.89 A worst-case trip, which board #1 is fitted with.  A trip looks like a spontaneous
// reboot, so a full-scale bring-up tone would present as a firmware crash.
//
// 25 % is -12 dB, i.e. 2.29 V rms demanded against a 3.54 V rail: 1.3 W, ~1.2 A peak from the
// cell, ~1.5x margin.  The default of 10 % is -20 dB = 0.21 W, which a 2" driver is plenty
// loud at on a desk.  ⚠ Raise this only with the AO4838 fitted AND the 15 V brick in -- both
// gates are in FIRMWARE.md §12.2, and R-AUDIO-1 has the numbers for each combination.
inline constexpr uint8_t kMaxVolPct = 25;
inline constexpr uint8_t kDefaultVolPct = 10;

Status enable(bool on) noexcept;  // I2S clocks + the datasheet's start-up order (§9.2.1.2.1)
bool active() noexcept;           // amp out of shutdown and unmuted

// Percent is amplitude: 100 % = 0 dB at the amp's volume register, 10 % = -20 dB
// (tas5760m::db_for_pct).  Above kMaxVolPct answers Denied -- refusing by name rather than
// clamping, because a volume that silently did something else is the bug that hides a trip.
Status set_volume_pct(uint8_t) noexcept;
uint8_t volume_pct() noexcept;

// The bring-up signal.  `ms` == 0 plays until stop(); enable() is implied, and the amp parks
// itself again a moment after the tone ends so a chime train does not leave a Class-D bridge
// idling into the speaker.
//
// NON-BLOCKING, and that costs something a caller has to know: an Ok here means the request
// was QUEUED, not that a sound was made.  The amp comes up on the writer task, ~25 ms later,
// and it can fail there.  `State::last_error` / `last_step` is where that failure surfaces --
// a CLI row must wait for one of them before it reports success (ground rule 2).  `ui`'s
// chime deliberately does not wait; it fires every 1.5 s and has nowhere to print.
Status tone(uint32_t hz, uint32_t ms) noexcept;
Status stop() noexcept;  // fades the tail out rather than cutting it
bool playing() noexcept;

// Bumped once per completed start attempt, after `last_error` is published.  A caller that
// samples it before tone() and polls until it moves has waited for the answer rather than
// for a duration -- which is the difference between "the amp is slow today" and a CLI row
// that guesses.
uint32_t start_seq() noexcept;

// ---- the stream source (pcm.hpp) --------------------------------------------------------
// 48 kHz mono s16 -- wav.hpp's format, which is the reason there is no conversion anywhere.
// The ring is 2 s (§6.3) and lives in PSRAM on target.
//
// PRODUCER side, and there is exactly one producer: `storage`.  stream_open() drops whatever
// was playing (tone or stream), empties the ring and queues the amp start exactly as tone()
// does -- asynchronous, answered through start_seq()/State::last_error.  Nothing is heard until
// the ring holds pcm::kPrimeMs or stream_end() says that is all there is.
inline constexpr uint32_t kStreamRingMs = 2000;
Status stream_open(uint32_t ramp_ms) noexcept;
std::size_t stream_write(const int16_t* mono, std::size_t n) noexcept;  // how many went in
std::size_t stream_space() noexcept;  // samples the ring will take right now; 0 when closed
void stream_end() noexcept;           // the last sample is written: drain, then park

struct Stream {
    bool open;       // opened and not yet finished, stopped or replaced
    bool primed;     // ... and actually making sound
    uint32_t level;  // samples in the ring
    uint32_t cap;    // ring size, samples
    uint32_t underruns;
    uint64_t played;  // frames consumed since stream_open()
    uint32_t seq;     // bumped by every stream_open(): tells a producer its stream was replaced
};
Stream stream() noexcept;

// ---- two bench tools, both for the same question: is the clock ARRIVING? ------------------
// The TAS5760M answers "clock error" (reg 0x08 CLKE) for two completely different faults --
// a signal that never reaches the pin, and a signal whose ratios it does not accept -- and
// from the outside they are the same bit.  These split them, and the first one needs only a
// multimeter, which matters because the whole chain is 12 MHz and 1.5 MHz.

// Drive ONE I2S pin as a static GPIO, so a DMM on the amp's own pin (U9 14/15/17/16) proves
// the module pad, the trace and the joint in one reading.  Tears the port down; the next
// tone() re-installs it.  Bench-only -- there are no pins on the host.
enum class Pin : uint8_t { Mclk, Bclk, Lrck, Dout, All };
inline constexpr const char* name(Pin p) noexcept {
    switch (p) {
        case Pin::Mclk:
            return "mclk";
        case Pin::Bclk:
            return "bclk";
        case Pin::Lrck:
            return "lrck";
        case Pin::Dout:
            return "dout";
        default:
            return "all";
    }
}
Status pin_drive(Pin, bool level) noexcept;
Status pin_release() noexcept;  // hand them back to I2S

// Is the PERIPHERAL toggling the pad?  pin_drive() proves the GPIO path -- the module pad,
// the trace, the joint -- and proves nothing about I2S, which is the half that matters when
// the amp says CLKE.  So: enable the input buffer on the four pads while I2S is driving them
// (the output matrix is untouched; this is how loopback works) and sample each one in a tight
// loop.  A pad carrying 12.288 MHz sampled at ~1 MHz comes back a mix of both levels; a pad
// that has STOPPED comes back all-one-value, which is CLKE cause 3 in one command and no
// instruments at all.
// ⚠ `high` is NOT a duty-cycle measurement and must not be read as one.  The sampling loop
// runs at a few MHz and the CPU clock shares a PLL with MCLK, so for the fast pads the samples
// alias at a fixed phase instead of sweeping through one.  Only LRCK (48 kHz) is oversampled
// enough for its percentage to mean anything.  What the numbers ARE good for is the binary
// question -- did both levels appear at all -- which is CLKE cause 3.
struct Probe {
    uint32_t samples;  // per pad
    uint32_t high[4];  // Pin order: mclk, bclk, lrck, dout
    bool toggling[4];  // both levels seen -- this is the answer; the percentage is not
};
Result<Probe> probe_pins() noexcept;

// The clock geometry, so the datasheet's Table 6 can be SWEPT from the bench instead of
// guessed at and reflashed.  `mclk_multiple` is 128/192/256/384/512 x f_S (the amp requires
// 128-512 and has no PLL); `slot_bits` is 16 or 32 and sets BCLK to 32 or 64 x f_S.  Takes
// effect on the next port install, which is the next tone().
Status set_clocking(uint16_t mclk_multiple, uint8_t slot_bits) noexcept;
void clocking(uint16_t& mclk_multiple, uint8_t& slot_bits) noexcept;

// What the port and the chip are actually doing -- for `audio status`, and for telling
// "the amp is muted" apart from "there are no clocks" apart from "the driver never ran".
struct State {
    bool clocks;   // I2S channel enabled: MCLK/BCLK/LRCLK are running
    bool sd_pin;   // SPK_SD (expander GPA0) high = out of shutdown
    bool muted;    // reg 0x03
    bool playing;  // a tone or a stream is being played
    bool configured;
    bool fault_pin;  // SPK_FAULT (expander GPB6), open-drain active-low -- inverted here
    uint8_t vol_pct;
    float vol_db;
    uint32_t mclk_hz;  // read back from the port, 0 when the clocks are down
    uint32_t bclk_hz;
    // The SOURCE those two are divided from (PLL_F160M), because the DIVISOR is the
    // interesting number and the frequency is not.  The S3 has no APLL for I2S, so MCLK is
    // always sclk/(N + b/a) -- and for an ODD N the divider's output duty is ceil(N/2)/N,
    // which at N=13 is 53.8 % against the amp's 45-55 % window (§6.5, DMCLK).
    uint32_t sclk_hz;
    uint32_t underruns;  // DMA writes that timed out -- a source that cannot keep up

    // Why the amp is not up, from the last attempt to bring it up.  `Ok` with a null step
    // means the last attempt worked (or none has been made).  This exists because tone() is
    // asynchronous: without it, a start that failed on the writer task 25 ms after the CLI
    // printed "tone 1000 Hz" is completely invisible, which is F0.1 all over again.
    Status last_error;
    const char* last_step;  // the §9.2.1.2.1 step that answered: "i2s", "cfg", "sd", "unmute"

    // The three registers that decide whether a sound is possible, read LIVE off the chip
    // rather than out of the driver's shadow.  A shadow that says `configured` over a chip
    // that has been back through POR (a brown-out, or DVDD dropping with SPK_SD) is exactly
    // the state where everything reports fine and nothing plays.
    bool regs_live;       // the three reads below succeeded
    uint8_t reg_digital;  // 0x02 -- expect 0x04
    uint8_t reg_analog;   // 0x06 -- expect 0xD1
    uint8_t reg_vol;      // 0x04
    uint8_t reg_fault;    // 0x08
};
Result<State> state() noexcept;

}  // namespace audio

// ---- persistent settings ----------------------------------------------------------------
// NVS on target, a file on the host.  The little that must survive a power cut and cannot be
// derived: today the per-unit hand calibration (§6.1), tomorrow the whole of §7.5's Config.
//
// int32 only, deliberately.  Everything stored so far is a microstep count or a flag, and a
// typed surface with exactly one type is a surface with no casts in it.  A key that has never
// been written answers NotPresent -- the same D16 answer as a device that is not fitted, and
// the caller's cue to keep its compiled-in default.
//
// ... and one string, since 2026-09-27: the alarm tone is a FILE NAME, and there is no honest
// way to write that as an int.  NUL-terminated, at most kStrMax - 1 bytes; get_str() answers
// NotPresent exactly as get_i32() does.  An empty string is a value (the setting was cleared),
// not an absence.
namespace store {
inline constexpr std::size_t kStrMax = 64;
Result<int32_t> get_i32(const char* key) noexcept;
Status set_i32(const char* key, int32_t value) noexcept;
Status get_str(const char* key, char* out, std::size_t cap) noexcept;
Status set_str(const char* key, const char* value) noexcept;
}  // namespace store

// ---- microSD (SPI2 + DMA, FATFS) ------------------------------------------------------------
// The card is USER assets only (§1: `/sd/tones/*.wav`) and absence is a normal state (D16):
// the clock is fully a clock with no card, and the alarm falls back to a generated tone.
//
// There is NO card-detect line on this board (esp32.md: IO13/14/21/18 and nothing else), so
// "is there a card" is only ever answered by trying to mount it.  mount() is idempotent and is
// retried by `storage` whenever it needs the card; unmount() before pulling it.
//
// Paths are the target's VFS paths on both backends -- "/sd/tones/x.wav" -- and the host maps
// them onto a directory.  Everything that touches a file is a BLOCKING call; §3.2 puts all of
// them on `storage`'s task, and the few CLI rows that read a directory say so.
namespace sd {
inline constexpr const char* kRoot = "/sd";
inline constexpr const char* kTonesDir = "/sd/tones";
inline constexpr std::size_t kNameMax = 64;  // a name in a listing, NUL included (FAT LFN)

struct Info {
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t freq_khz;  // the SPI clock the card accepted
    char name[8];       // the card's CID product name ("SD16G"); "" on the host
};

Status mount() noexcept;  // Ok, or NotPresent when no card answered
Status unmount() noexcept;
bool mounted() noexcept;
Result<Info> info() noexcept;

struct Entry {
    char name[kNameMax];
    uint32_t size;
    bool dir;
};
// Calls `fn` once per entry (not `.`/`..`), in directory order; `fn` returns false to stop.
// NotPresent when the card is not mounted, Failed when the directory is not there.
using ListFn = bool (*)(Entry const&, void* ctx);
Status list(const char* dir, ListFn fn, void* ctx) noexcept;

// A small file table -- the whole product needs one open stream and a header probe.
inline constexpr int kMaxOpen = 3;
Result<int> open(const char* path) noexcept;  // a handle; NotPresent (no card) / Failed (no file)
Result<std::size_t> read(int fd, void* buf, std::size_t n) noexcept;  // 0 = end of file
Status seek(int fd, uint32_t offset) noexcept;
Result<uint32_t> size(int fd) noexcept;
void close(int fd) noexcept;

// Writing, for the phone's uploads (app/PROTOCOL.md "Sound files").  `storage` is the only
// writer.  create() truncates unless `append`; write() answers how many bytes went down.
Result<int> create(const char* path, bool append) noexcept;
Result<std::size_t> write(int fd, const void* buf, std::size_t n) noexcept;
Status remove(const char* path) noexcept;  // Failed when there is no such file
// FATFS will not rename onto an existing name, so the caller removes the target first.
Status rename(const char* from, const char* to) noexcept;
Status mkdir(const char* path) noexcept;  // Ok when it already exists
}  // namespace sd

// ---- power -----------------------------------------------------------------------------
// One implementation for both backends (shared/power.cpp): it is arithmetic over hal::adc and
// hal::expander, and nothing in it is platform-specific.  The host and the target therefore
// cannot disagree about the SoC endpoints, about the three inversions below, or about what
// `VBAT_SENSE` is a measurement OF -- which is the part that is easy to get wrong.
namespace power {

// The SoC endpoints.  4.05 V and not 4.2 because the health cap is fixed in HARDWARE by the
// LT3652's float divider (README S10); `set_full_charge()` switches R16 in for a 4.2 V top-up
// and is the only way past it.  A straight line between the two -- a real OCV curve wants the
// `board` AO and a cell that has been characterised, and a straight line is honest about being
// a straight line in a way that a fitted curve over guessed data would not be.
inline constexpr uint16_t kVbatEmptyMv = 3300;
inline constexpr uint16_t kVbatFullMv = 4050;

// What the number is a measurement OF, and it is not always the cell.        [R-BOARD-3]
//
// `VBAT_SENSE` taps cell+ against BOARD GND rather than across the cell, so it is only looking
// at the cell while cell - is actually AT PACK-.  Whenever the protector's charge FET is open
// -- an OV/OC trip, a cell below the OD threshold, an empty holder -- cell - floats a full
// cell-voltage away and the divider reports the BAT node instead: the charger's own output.
// Measured on build #1 2026-09-13: 4.0 V reported for a cell sitting at 3.4 V.
//
// There is no hardware fix short of a differential sense, so the driver's job is to say which
// one you are looking at rather than to pretend.  The line it draws is the one FIRMWARE.md
// R-BOARD-3 draws: the tap is the cell only when the board is running FROM the cell, which is
// `PD_PG` deasserted.  Plugged, it is the BAT node -- usually equal to the cell, because the
// charge FET is usually closed, and nothing on this board can tell the difference.
//
// `CHRG` deliberately does NOT buy its way into `Cell`.  Current flowing does prove the charge
// FET is closed, but it also means the node sits I x (R_fet + R_wire) above the cell, and the
// LT3652 keeps CHRG asserted all the way down its C/10 taper -- so "charging" is a cell
// voltage plus an unknown offset, which is not cell health either.
enum class VbatSrc : uint8_t {
    Unknown,  // never returned by a successful read(); it is what a default-constructed State
              //   says, so a caller that skipped ok() cannot read a zero as cell health
    Cell,     // unplugged: the board is running from the cell and the tap is across it
    BatNode,  // plugged: the LT3652's output.  Equal to the cell while its charge FET is
              //   closed, which this tap cannot see (R-BOARD-3)
};
inline constexpr const char* name(VbatSrc s) noexcept {
    switch (s) {
        case VbatSrc::Cell:
            return "cell";
        case VbatSrc::BatNode:
            return "bat-node";
        default:
            return "?";
    }
}

// `soc_pct` when the millivolts are not the cell's.  A number would be a faked reading, and
// D16's rule is the same here as it is for an absent device: say so, never invent it.
inline constexpr uint8_t kSocUnknown = 0xFF;

struct State {
    uint16_t vbat_mv;  // at the cell, the /2 divider already undone -- but see `src`
    uint8_t soc_pct;   // kSocUnknown unless src == Cell
    VbatSrc src;       // R-BOARD-3: what vbat_mv is actually a measurement of
    bool plugged;      // PD_PG,  open-drain active-low -- inverted here
    bool charging;     // CHRG,   open-drain active-low -- inverted here
    bool fault;        // FAULT,  open-drain active-low -- inverted here
};
Result<State> read() noexcept;

// Full cell or empty holder -- the one thing on this board that can tell them apart.
//
// With `Q2` conducting, its channel back-feeds the holder from the BAT node, so a plugged board
// reads "full cell" with nothing in the holder at all.  `CELL_TEST` turns `Q2` off; the tap then
// sees the holder alone, which is the cell if there is one and one body-diode below the BAT node
// if there is not.  The measurement is the STEP at switch-off (power.md, kicad/gen/b_charger.py).
//
// PLUGGED-ONLY, and this is R-BOARD-2 rather than a preference: on battery `Q2` off cuts all
// system power, the board browns out, the expander goes hi-Z, `R26` pulls `Q8` off and it boots
// again -- a self-recovering loop, but an unbounded one while the bit is set.  There is no
// hardware interlock, so read()'s answer is not good enough: cell_test() takes its own fresh
// `PD_PG` and answers `Denied` on battery.
struct CellTest {
    uint16_t rest_mv;  // CELL_TEST low, before -- the BAT node with Q2 conducting
    uint16_t held_mv;  // CELL_TEST high -- Q2 off, the holder side alone
    uint16_t open_mv;  // CELL_TEST low again; open_mv - held_mv is the measurement
    int16_t step_mv;
    bool present;   // the step stayed under one body-diode drop: something is in the holder
    bool charging;  // CHRG during the test.  FALSE MAKES THE VERDICT UNSAFE: with the charge
                    //   FET open the tap is not on the cell at all and neither reading means
                    //   what it says (R-BOARD-3).  Reported rather than refused -- the bench
                    //   wants the three numbers either way.
};
Result<CellTest> cell_test() noexcept;

// The 4.20 V "top to 100 %" mode: `FULLCHG_EN` (GPB4) drives `Q1`, which switches `R16` into
// the LT3652's float divider.  Default OFF and it needs no firmware help to be -- the expander
// is hi-Z at POR and `R24` holds `Q1` off, so the 4.05 V health cap is what a board that has
// never been told anything enforces.  Policy, deliberately outside read(): nothing derives it,
// somebody asks for it.
Status set_full_charge(bool on) noexcept;
Result<bool> full_charge() noexcept;

}  // namespace power

// ---- BLE peripheral (NimBLE) --------------------------------------------------------------
// The radio and the GATT table, and no policy.  One vendor service, four characteristics
// (FIRMWARE.md §8.2): `cmd` (write), `rsp` (notify), `status` (read + notify), `info`
// (read).  All four require an ENCRYPTED, BONDED link -- that check is here, in the access
// callback, so nothing above can forget it.  WHEN a bond may be made (the pairing window),
// what a command line means and when to notify are the `net` AO's.
//
// Threading: the callbacks below run on the BLE host task (core 0).  They must copy and
// return.  Everything else may be called from any task.
namespace ble {

inline constexpr std::size_t kMaxWrite = 256;  // longest command write accepted, bytes

struct Link {
    bool up;  // stack running (false: never started, or RADIO_OFF stopped it)
    bool advertising;
    bool pairable;  // a new bond would be accepted right now
    bool connected;
    bool encrypted;
    bool bonded;       // encrypted with a stored bond: the only state the service answers in
    bool rsp_sub;      // the central subscribed to `rsp`
    bool status_sub;   // ... and to `status`
    uint16_t mtu;      // negotiated ATT MTU; a notification carries mtu - 3 bytes
    uint8_t bonds;     // peers in the bond store
    uint32_t paired;   // new bonds made since boot -- monotonic, so a poller sees every one
    uint32_t refused;  // links dropped for trying to pair outside the window
};

using RxFn = void (*)(const uint8_t* data, std::size_t len);

// Bring the stack up, register the service, advertise as `name`.  `on_cmd` receives each
// complete write to `cmd`, on the host task, only from a bonded link.  NotPresent on a build
// with no radio.
Status start(const char* name, RxFn on_cmd) noexcept;
Status stop() noexcept;  // drop the link, stop advertising, power the controller down

// The pairing window.  Closed: an unknown phone may connect but cannot bond, and is dropped
// the moment its link encrypts without one.  Open: Just Works bonding, advertising faster.
Status set_pairable(bool) noexcept;

// Busy: no buffer right now (retry); NotReady: nobody bonded + subscribed to hear it.
Status notify_rsp(const uint8_t* data, std::size_t len) noexcept;
// Replaces what a read of `status` returns; also notifies it when `notify` and subscribed.
Status set_status(const uint8_t* data, std::size_t len, bool notify) noexcept;
Status set_info(const char* text) noexcept;  // what a read of `info` returns
Status unbond_all() noexcept;                // forget every phone; drops a bonded link
Link link() noexcept;

// The `blob` characteristic: binary upload data, write WITH response (app/PROTOCOL.md "Sound
// files").  Each write is a 4-byte little-endian offset and then the bytes.  The handler runs
// on the host task, must copy and return, and its Status IS the ATT answer -- which is what
// makes write-with-response the flow control: the phone cannot send the next chunk until this
// one was accepted or refused, and a refusal says why (kBlobErr* below).
inline constexpr std::size_t kMaxBlob = 512;  // ATT's attribute limit; a long write reassembles
using BlobFn = Status (*)(const uint8_t* data, std::size_t len);
void set_blob_handler(BlobFn) noexcept;
// Application ATT error codes (0x80-0x9F) a refused blob write answers with.
inline constexpr uint8_t kBlobErrBusy = 0x80;      // Busy: queue full -- retry the same write
inline constexpr uint8_t kBlobErrOffset = 0x81;    // BadArg: not the expected offset, or past
                                                   //   the size -- ask `storage put` for `next`
inline constexpr uint8_t kBlobErrNoUpload = 0x82;  // NotReady: no `storage put` is open
inline constexpr uint8_t kBlobErrFailed = 0x83;    // Failed: the card refused a write
inline constexpr uint8_t blob_att_err(Status st) noexcept {
    switch (st) {
        case Status::Ok:
            return 0;
        case Status::Busy:
            return kBlobErrBusy;
        case Status::BadArg:
            return kBlobErrOffset;
        case Status::NotReady:
            return kBlobErrNoUpload;
        default:
            return kBlobErrFailed;
    }
}

}  // namespace ble

// ---- the chip itself ------------------------------------------------------------------------
namespace sys {
struct Info {
    uint32_t heap_free;    // bytes, all heaps
    uint32_t heap_min;     // low-water since boot
    uint8_t reset_reason;  // esp_reset_reason_t; 0 on the host
};
Info info() noexcept;
}  // namespace sys

// Brings the fake or the real peripherals up.  Idempotent.
Status init() noexcept;

// Restart the whole image.  esp_restart() on target; on the host clocksim re-execs itself,
// which is the closest a laptop gets to it -- same effect either way, so `sys reboot` is one
// command with one meaning on both.  Does not return when it works; a caller that gets a
// Status back is still running the old image.
Status reboot() noexcept;

}  // namespace clk::hal
