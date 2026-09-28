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
void set_warp(double factor) noexcept;  // 1.0 = real time; clamped to [0.01, 10000]
double warp() noexcept;
void advance(uint64_t sim_us) noexcept;  // instantaneous jump, for `sim jump`

// ---- analog ----------------------------------------------------------------------------
// The opto has two sources.  AUTO (the default) derives it from where the hands physically
// are, so the real homing FSM has to search for the index and can genuinely fail to find it.
// Writing a value switches to MANUAL, which is what a test that only cares about a threshold
// wants.  `set_opto_auto(true)` goes back.
void set_opto(float norm) noexcept;  // 0 = dark, 1 = full reflection; clamped; -> MANUAL
float opto() noexcept;               // the effective value, whichever source is live
void set_opto_auto(bool) noexcept;
bool opto_auto() noexcept;
void set_vbat_mv(uint16_t mv) noexcept;
void set_noise_mv(uint16_t peak) noexcept;  // +/- peak, deterministic PRNG
void set_seed(uint32_t) noexcept;           // reproducible streams for tests

// ---- knob ------------------------------------------------------------------------------
void turn(int32_t detents) noexcept;        // 4 PCNT counts per detent
void turn_counts(int32_t counts) noexcept;  // raw counts -- what a dragged knob produces
// The same counts spread over `ms` of sim time: a knob being TURNED rather than teleported.
// A finger cannot deliver forty counts inside one 20 ms poll, and the ui now paces a setting
// to what the hands can draw (§6.6d), so the difference decides how much of a spin survives.
void turn_counts_over(int32_t counts, uint32_t ms) noexcept;
void press(uint32_t hold_ms) noexcept;  // held for hold_ms of SIM time

// ---- the mechanism ---------------------------------------------------------------------
// Where the hands PHYSICALLY sit, against what the firmware commanded.  A cold boot has no
// idea; modelling that is the entire reason homing exists, and setting an offset here is the
// sim equivalent of reaching in and nudging a hand.  hand_angle() is what you see through
// the glass: 0 deg = 12 o'clock, clockwise positive, index window at 0 deg.
void set_hand_angle(hal::motor::Hand, float deg) noexcept;  // reach in and point the hand here
float hand_angle(hal::motor::Hand) noexcept;
void set_hand_offset(hal::motor::Hand, float deg) noexcept;  // the raw error, for tests
float hand_offset(hal::motor::Hand) noexcept;

// ---- IMU -------------------------------------------------------------------------------
void set_orientation(float yaw_deg, float pitch_deg, float roll_deg) noexcept;
void tap() noexcept;  // one top-tap; bumps the counter the driver diffs

// ---- the room (TSL2591 + BME688 register models) ----------------------------------------
// The SCENE, not the chip.  These go in as physical quantities and come back out through the
// shipping drivers -- lux through the auto-range, temperature through the compensation
// arithmetic -- so a driver bug shows up here as a number that does not match what was set.
void set_lux(float lux) noexcept;
float lux() noexcept;
void set_env(float temp_c, float rh_pct, float press_hpa, uint32_t gas_ohms) noexcept;

// ---- expander --------------------------------------------------------------------------
// Drives an INPUT signal -- the rear radio toggle, PD_PG, CHRG.  Outputs are the firmware's.
void set_expander_in(hal::expander::Sig, bool level) noexcept;

// ---- audio -----------------------------------------------------------------------------
void set_speaker(bool on) noexcept;  // stands in for the `audio` AO until it exists

// ---- power -----------------------------------------------------------------------------
void set_plugged(bool) noexcept;
bool plugged() noexcept;
// Take the cell out of the holder.  Not the same as a flat cell, and telling the two apart is
// the entire job of `hal::power::cell_test()` -- on a plugged board `Q2` back-feeds the holder
// from the BAT node, so an empty holder reads "full" until CELL_TEST turns Q2 off.
void set_cell_in(bool) noexcept;
bool cell_in() noexcept;
// How many times the Vbat divider leg has been switched in, so a test can see that a reading
// went through `VBAT_DIV_EN` rather than round it.  Same kind of observation as refreshed().
uint32_t vbat_div_reads() noexcept;

// ---- BLE: a fake phone -------------------------------------------------------------------
// The stack's POLICY half is real (hal::ble's contract: the window gates bonding, only a
// bonded link reaches `cmd`); the radio is a function call.  One central at a time, like the
// board.  `sim ble ...` drives these, and so does test_net.
void ble_connect() noexcept;    // a phone connects -- a stranger until it pairs
bool ble_pair() noexcept;       // ... asks to bond.  False (and dropped) if the window is shut
void ble_reconnect() noexcept;  // a phone that bonded earlier connects and encrypts
void ble_disconnect() noexcept;
void ble_subscribe(bool rsp, bool status) noexcept;
void ble_set_mtu(uint16_t) noexcept;
// A write to `cmd`.  Denied on a link that is not bonded -- the ATT error a phone would get.
Status ble_write(const char* text) noexcept;
// The oldest `rsp` notification not yet taken, NUL-terminated.  False when there is none.
bool ble_pop_rsp(char* out, std::size_t cap) noexcept;
std::size_t ble_read_status(uint8_t* out, std::size_t cap) noexcept;  // a read of `status`
uint32_t ble_status_notifies() noexcept;  // how many `status` notifications went out
std::size_t ble_read_info(char* out, std::size_t cap) noexcept;

// ---- persistent settings ---------------------------------------------------------------
// Where hal::store keeps its `key = value` file.  The app sets it; a test binary that leaves
// it unset gets an honest NotPresent out of every read, which is what keeps a unit test from
// writing calibration into somebody's home directory.
void set_store_path(const char* path) noexcept;
const char* store_path() noexcept;

// ---- readback (for the CLI and for tests) ----------------------------------------------
// 7 chars + NUL, one per pixel, dominant-channel letter: '.' off, R G B W C M Y, 'o' other.
void render_pixels(char* out, std::size_t cap) noexcept;
bool refreshed() noexcept;  // has refresh() been called since the last set?

// Everything the UI bridge needs, under ONE lock acquisition.  Deliberately not built out
// of the hal:: reads: knob::read() consumes `delta`, and a viewer that steals deltas from
// the `ui` AO would be a viewer that changes what it is watching.
struct Snapshot {
    uint64_t sim_us;
    double warp;
    // mechanism -- hand_deg is the TRUE angle, pos is what the firmware commanded
    float hand_deg[2];
    int32_t hand_pos[2];
    int32_t hand_vel[2];
    bool hand_moving[2];
    bool motor_on;
    // light
    pixels::Rgbw px[pixels::kCount];
    bool refreshed;
    uint8_t warm_pct, cool_pct;
    // sound
    bool spk_active;
    uint8_t vol_pct;
    // power
    uint16_t vbat_mv;  // the CELL, always -- the God view, not what VBAT_SENSE can see
    uint8_t soc_pct;
    bool plugged, charging, cell_in;
    // input
    int32_t knob_count;
    bool knob_sw;
    float opto;
    bool opto_auto;
    float yaw_deg;
    uint16_t taps;
    bool radio_off;
};
Snapshot snapshot() noexcept;

// Restores every fake to its power-on value.  Called by `sim reset` and by each test.
// The HARDWARE only: the services keep running and keep whatever they believe, which is
// exactly the interesting case -- `motion` still thinks it is homed while the hands have
// jumped.  For the other one, see reboot().
void reset() noexcept;

// A cold start of the whole image -- the host's answer to pulling the power, and the only
// way to clear what the services believe.  clocksim installs a hook that re-execs the
// process; nothing else does, and reboot() then reports NotPresent rather than pretending.
// Never returns when it succeeds.
using RebootFn = void (*)();
void set_reboot_hook(RebootFn) noexcept;
Status reboot() noexcept;

}  // namespace clk::hal::host
