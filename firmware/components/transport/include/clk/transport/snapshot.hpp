// The status snapshot: everything the clock knows, in one timestamped record.  [FIRMWARE.md §8.3]
//
// One struct, one fixed little-endian wire layout, no allocation.  It is what the BLE
// `status` characteristic serves and notifies, what `sys snap` prints, and -- because it is
// small (132 bytes) and self-describing by schema + size -- what a future app logs to plot the
// clock over time.  The layout table in FIRMWARE.md §8.3 is generated from nothing; it is
// kept honest by test_net's round-trip and by the static_assert on kWireSize below.
//
// Evolution is APPEND-ONLY.  A new field goes on the end and bumps `size`, never `schema`;
// an old reader decodes the prefix it knows and skips the rest.  `schema` changes only if a
// field is ever re-typed or moved, which is exactly the break an old reader must detect.
//
// Validity is in `flags`, not in magic numbers: `kEnvOk` clear means temp/rh/press/gas are
// meaningless, whatever they happen to hold.  The few sentinels (soc 0xFF, ages 0xFFFF) are
// the ones the HAL already speaks.
#pragma once

#include <cstddef>
#include <cstdint>

namespace clk::transport {

inline constexpr uint8_t kSchema = 1;
inline constexpr std::size_t kWireSize = 132;  // schema 1, as first shipped
inline constexpr uint16_t kAgeNever = 0xFFFF;  // env_age_s / als_age_s: never read
inline constexpr std::size_t kPixels = 7;

enum Flag : uint32_t {
    kTimeValid = 1u << 0,       // epoch_ms is a real time (someone set it)
    kTimeFollow = 1u << 1,      // the hands are tracking the clock (not showing a mode)
    kTzSet = 1u << 2,           // tz_off_min was given (`chrono time epoch` / `chrono tz`)
    kNetProvisioned = 1u << 3,  // Wi-Fi credentials stored
    kNetSynced = 1u << 4,       // ... and SNTP has landed at least once
    kNetLocked = 1u << 5,       // the network owns the time: `clock` mode refuses
    kRadioOff = 1u << 6,        // rear RADIO_OFF toggle asserted
    kBleConnected = 1u << 7,    // a central is connected
    kBleSecure = 1u << 8,       // ... over an encrypted, bonded link
    kBlePairing = 1u << 9,      // the pairing window is open
    kPowerOk = 1u << 10,        // the power block answered: vbat/soc/plugged below are real
    kPlugged = 1u << 11,        // PD_PG: USB-PD 15 V present
    kCharging = 1u << 12,       // CHRG
    kChargeFault = 1u << 13,    // FAULT (NTC window / timer)
    kFullCharge = 1u << 14,     // FULLCHG_EN: 4.20 V top-up instead of the 4.05 V cap
    kBattLow = 1u << 15,        // unplugged and under the warning threshold
    kHomed = 1u << 16,          // motion knows where the hands are
    kMotorPowered = 1u << 17,   // coils energised
    kKnobPressed = 1u << 18,    // ENC_SW down right now
    kKnobInput = 1u << 19,      // `ui input` on (the knob drives the UI)
    kAlarmArmed = 1u << 20,
    kAmpActive = 1u << 21,  // amp out of shutdown
    kAudioPlaying = 1u << 22,
    kImuOk = 1u << 23,    // gravity / ypr / taps are real
    kImuLink = 1u << 24,  // the BNO085 hub accepted its features
    kAlsOk = 1u << 25,    // lux is real
    kAlsSaturated = 1u << 26,
    kEnvOk = 1u << 27,  // temp / rh / press / gas are real
    kEnvGasValid = 1u << 28,
    kEnvHeatStable = 1u << 29,
    kDateValid = 1u << 30,  // epoch_ms carries a real date (added 2026-09-28)
};

struct Snapshot {
    // ---- when, and which clock -------------------------------------------------------------
    uint16_t seq = 0;          // +1 per snapshot taken; wraps.  A gap = a missed sample
    uint32_t uptime_s = 0;     // since boot
    int64_t epoch_ms = 0;      // UTC ms, local = this + tz_off_min; needs kTimeValid
    int16_t tz_off_min = 0;    // the zone's offset now, DST included (default zone: San Francisco)
    uint8_t reset_reason = 0;  // esp_reset_reason_t
    uint8_t clk_src = 0;       // hal::clock_::SlowSrc
    uint32_t flags = 0;        // Flag
    uint32_t fw_id = 0;        // first 8 hex digits of the git sha: marks a reflash on a plot
    uint32_t heap_free = 0;    // bytes
    uint32_t heap_min = 0;     // low-water mark since boot
    // ---- power ------------------------------------------------------------------------------
    uint16_t vbat_mv = 0;
    uint8_t soc_pct = 0xFF;  // 0xFF = unknown (plugged: the tap is the BAT node, R-BOARD-3)
    uint8_t vbat_src = 0;    // hal::power::VbatSrc: 0 unknown, 1 cell, 2 bat-node
    // ---- room (BME688) ----------------------------------------------------------------------
    int16_t temp_cdeg = 0;    // 0.01 degC
    uint16_t rh_cpct = 0;     // 0.01 %RH
    uint16_t press_dhpa = 0;  // 0.1 hPa
    uint32_t gas_ohms = 0;
    uint16_t env_age_s = kAgeNever;  // how old the reading above is -- it is sampled slowly
    // ---- light (TSL2591) --------------------------------------------------------------------
    float lux = 0.0f;  // -1 when saturated
    uint16_t als_age_s = kAgeNever;
    // ---- motion sensor (BNO085), dial axes (+X right, +Y the printed 12, +Z out the glass) ----
    int16_t grav_mm[3] = {};   // mm/s^2
    int16_t ypr_cdeg[3] = {};  // yaw, pitch, roll, 0.01 deg
    uint16_t taps = 0;         // monotonic, wraps
    // ---- movement -------------------------------------------------------------------------
    uint8_t motion_state = 0;            // svc::Motion::State: uninit homing idle moving fault
    uint8_t dial_tick = 0;               // which dot is at the top (0 = the printed 12)
    uint8_t hand_h = 0, hand_m = 0;      // what the hands show
    uint8_t target_h = 0, target_m = 0;  // ... and where they are going
    uint16_t opto = 0;                   // homing sensor, 0..65535 = 0..1
    uint32_t motion_faults = 0;
    uint16_t trims = 0;     // auto-home corrections since boot
    int16_t last_trim = 0;  // usteps
    // ---- ui -----------------------------------------------------------------------------------
    uint8_t ui_mode = 0;  // svc::Ui::Mode: idle bell alarm clock volume pairing ringing snoozed
    uint8_t volume = 0;   // %
    uint8_t alarm_h = 0, alarm_m = 0;
    uint8_t brightness = 0;                // %
    uint8_t wake_warm = 0, wake_cool = 0;  // wake-light %, both channels
    uint8_t ble_state = 0;                 // BleState
    uint8_t px[kPixels][4] = {};  // R G B W as last written, chain order (dial0 dial1 bell ...)
    int32_t knob_count = 0;       // raw quadrature, 256/rev
    // ---- radio --------------------------------------------------------------------------------
    uint8_t bonds = 0;       // phones in the bond store
    uint8_t wifi_state = 0;  // WifiState
    int8_t wifi_rssi = 0;    // dBm, 0 = n/a
    uint8_t wifi_err = 0;    // WifiErr: why the last attempt failed (was `reserved`)
};

enum class BleState : uint8_t { Off, Idle, Pairing, Connected, Secure };
const char* name(BleState) noexcept;
// Off: radio off (rear toggle) or no Wi-Fi.  Idle: no network stored.  Backoff: the last
// attempt failed (wifi_err says why) and the next is scheduled.
enum class WifiState : uint8_t { Off, Idle, Connecting, Online, Backoff };
const char* name(WifiState) noexcept;
enum class WifiErr : uint8_t { None, NoAp, Auth, NoIp, Timeout, Other };
const char* name(WifiErr) noexcept;

// The snapshot's `fw_id` from a `git describe` string: the first 8 hex digits of the sha
// ("ed6214f-dirty" -> 0x0ed6214f, "v0.2-3-ged6214f" -> 0x0ed6214f).  0 when there is none.
uint32_t fw_id(const char* describe) noexcept;

// Writes the wire form.  Returns the byte count (kWireSize), or 0 if `cap` is too small.
std::size_t encode(Snapshot const&, uint8_t* out, std::size_t cap) noexcept;
// Reads a schema-1 record of at least kWireSize bytes (a longer one is a newer firmware: the
// tail is ignored).  False on a wrong schema or a short buffer.
bool decode(const uint8_t* in, std::size_t len, Snapshot& out) noexcept;

}  // namespace clk::transport
