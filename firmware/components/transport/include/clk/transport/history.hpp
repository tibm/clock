// The history log: what the clock records while nobody is watching.   [FIRMWARE.md §6.3a]
//
// One FIXED 24-byte little-endian record per period (default 5 min) -- the room, the light, the
// battery -- and the same 24 bytes for the rare EVENT (a boot, an alarm, Wi-Fi).  Day files on
// the card, `/sd/log/<yyyy>/<mmdd>.bin` by UTC date, each a 32-byte header then records.
//
// Fixed size on purpose: a file whose length is not 32 + 24n has a torn tail and is trimmed,
// any record can be found by seeking, and a reader needs no state.  Delta/varint coding would
// save ~60 % and buy none of that; at the default period two years is 5 MB.
//
// This is a WIRE FORMAT: the phone downloads these files byte for byte (app/PROTOCOL.md
// "History") and decodes them against app/protocol.json -> `history`, whose golden record
// test_history asserts against the encoder below.
#pragma once

#include <cstddef>
#include <cstdint>

namespace clk::transport::hist {

inline constexpr std::size_t kRecord = 24;
inline constexpr std::size_t kHeader = 32;
inline constexpr uint8_t kVersion = 1;
inline constexpr char kMagic[4] = {'C', 'L', 'K', 'L'};

enum Kind : uint8_t { kSample = 1, kEvent = 2 };

// Sample flags (u16 @20).  State at the END of the period unless noted.
enum Flag : uint16_t {
    kEnvOk = 1u << 0,     // temp / rh / press / gas are real (any reading in the period)
    kGasValid = 1u << 1,  // ... and the gas reading was valid
    kHeatStable = 1u << 2,
    kAlsOk = 1u << 3,    // lux fields are real
    kAlsSat = 1u << 4,   // the sensor saturated at least once
    kPowerOk = 1u << 5,  // vbat / soc are real
    kPlugged = 1u << 6,
    kCharging = 1u << 7,
    kBattLow = 1u << 8,
    kWifiOnline = 1u << 9,
    kRadioOff = 1u << 10,
    kAlarmArmed = 1u << 11,
    kTimeSntp = 1u << 12,  // the clock's time came from SNTP (else the phone / the knob)
};

enum class Ev : uint8_t {
    Boot = 1,     // a0 = reset reason, a1..4 = fw_id (u32)
    AlarmFire,    // a0 = hour, a1 = minute (local)
    AlarmSnooze,  // a0 = snooze minutes
    AlarmDismiss,
    WifiOnline,  // a0 = rssi (i8)
    WifiFail,    // a0 = transport::WifiErr, a1 = driver reason
    SntpSync,    // a0 = server index, a1..4 = step ms (i32, 0 on the first set)
    TimeSet,     // a0 = source (Chrono::Source), a1..4 = step ms (i32)
    LogConfig,   // a0..1 = period s (u16), a2..3 = keep days (u16)
};
const char* name(Ev) noexcept;

struct Sample {
    uint32_t t = 0;  // Unix seconds UTC: the START of the period
    uint8_t n = 0;   // readings averaged into it
    int16_t temp_cdeg = 0;
    uint16_t rh_cpct = 0;
    uint16_t press_dhpa = 0;
    uint32_t gas_ohms = 0;
    float lux = 0.0f;      // mean over the period
    float lux_max = 0.0f;  // peak
    uint16_t vbat_mv = 0;
    uint8_t soc_pct = 0xFF;
    int8_t rssi = 0;
    uint16_t flags = 0;
};

struct Event {
    uint32_t t = 0;
    Ev code = Ev::Boot;
    uint8_t args[16] = {};
};

// ---- the encodings that are not plain integers -------------------------------------------
// Gas resistance: u16 = round(log10(ohms) * 8192) -- 1 ohm .. ~98 Mohm, steps of 0.03 %.
uint16_t enc_gas(uint32_t ohms) noexcept;
uint32_t dec_gas(uint16_t v) noexcept;
// Lux mean: u16 = round(log10(lux + 1) * 12000) -- 0 .. 88 000 lx (the TSL2591's range).
uint16_t enc_lux(float lux) noexcept;
float dec_lux(uint16_t v) noexcept;
// Lux peak: u8 = round(log10(lux + 1) * 50) -- coarse (~12 % steps), it is a marker.
uint8_t enc_lux8(float lux) noexcept;
float dec_lux8(uint8_t v) noexcept;

// CRC-8, poly 0x07, init 0, no reflection (CRC-8/SMBUS) over bytes 0..22.
uint8_t crc8(const uint8_t* p, std::size_t n) noexcept;

void encode(Sample const&, uint8_t (&out)[kRecord]) noexcept;
void encode(Event const&, uint8_t (&out)[kRecord]) noexcept;

struct Record {
    Kind kind;
    Sample s;  // kind == kSample (lux fields decoded back to lux)
    Event e;   // kind == kEvent
};
// False on a bad CRC or an unknown kind -- skip that record, the next one is aligned anyway.
bool decode(const uint8_t* in, Record& out) noexcept;

// ---- files ---------------------------------------------------------------------------------
struct Header {
    uint16_t period_s = 0;
    uint32_t day = 0;  // Unix seconds of 00:00 UTC of this file's day
    uint32_t fw_id = 0;
};
void encode(Header const&, uint8_t (&out)[kHeader]) noexcept;
bool decode_header(const uint8_t* in, std::size_t n, Header& out) noexcept;

// "/sd/log/2026/0928.bin" for a UTC instant.  False if `cap` is too small.
bool day_path(uint32_t t, char* out, std::size_t cap) noexcept;
uint32_t day_start(uint32_t t) noexcept;  // 00:00 UTC of t's day
uint32_t yyyymmdd(uint32_t t) noexcept;   // 20260928
// 20260928 -> 00:00 UTC of that day; 0 when it is not a date.
uint32_t from_yyyymmdd(uint32_t d) noexcept;

// ---- the budget ----------------------------------------------------------------------------
// Events per day allowed for in the projection -- generous: a normal day has a handful.
inline constexpr uint32_t kEventsPerDay = 50;
// Bytes ON THE CARD for one day file at `period_s`: rounded up to whole clusters, because that
// is what the card spends.
uint64_t day_bytes(uint32_t period_s, uint32_t cluster) noexcept;
uint64_t projected_bytes(uint32_t period_s, uint32_t keep_days, uint32_t cluster) noexcept;

}  // namespace clk::transport::hist
