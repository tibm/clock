// BNO085 sensor hub over SHTP/SH-2 on I2C.                        [FIRMWARE.md §6.5.1, R-BOARD-3]
//
// Sensor daughterboard, J7, address 0x4A (SA0 low via R3), `SENSOR_INT` on IO42, its own
// 32.768 kHz crystal, and NO host reset line -- R-BOARD-3, which is why link() exists.
//
// ⚠ This is a BRING-UP driver, deliberately narrower than §6.5.1's destination.  §6.5.1 says
// to use CEVA's `sh2` reference driver rather than hand-rolling SHTP, and that is still
// right for the product: `sh2` handles the whole report catalogue, the calibration and DCD
// save, and the timebase arithmetic.  It is not vendored yet (`vendor/` is empty), it is a C
// library with its own HAL that the host cannot exercise, and what milestone 1 needs is one
// question answered -- "is the hub alive, which way is up, and did that tap register".  So
// this file speaks the small subset that answers it: advertisement drain, product-ID
// handshake, two Set-Feature commands, and two input reports.  Everything it parses, `sh2`
// parses the same way; swapping it in later replaces this transport and keeps the surface.
//
// Compiled into both backends so the host build type-checks it, but on the host `hal::imu`
// stays the angle-based fake: §13.9 item 9 rules an SHTP responder out of the device models,
// and modelling it badly would be worse than not modelling it.  The parts that CAN be tested
// on the host are pure and live below.
#pragma once

#include "clk/hal/hal.hpp"
#include "clk/status.hpp"

namespace clk::hal::bno085 {

inline constexpr uint8_t kAddr = 0x4A;  // SA0 low (R3 fitted); 0x4B is the DNP alternate

// SHTP channels (SH-2 reference manual §1.4).  Named because a magic 3 in a packet dispatch
// is the difference between "a tap" and "a command reply nobody read".
enum Chan : uint8_t { kChanCmd = 0, kChanExec = 1, kChanCtrl = 2, kChanInput = 3, kChanWake = 4 };

// SH-2 report IDs, the five that matter here.
inline constexpr uint8_t kReportGravity = 0x06;
inline constexpr uint8_t kReportTap = 0x10;
inline constexpr uint8_t kReportTimebase = 0xFB;
inline constexpr uint8_t kReportProductIdReq = 0xF9;
inline constexpr uint8_t kReportProductIdResp = 0xF8;
inline constexpr uint8_t kReportSetFeature = 0xFD;

// ---- pure, and therefore tested on the host --------------------------------------------

// The 4-byte SHTP header.  Bit 15 of the length is the continuation flag, and the length
// INCLUDES the header -- a driver that forgets either reads four bytes into the next packet
// and never re-syncs, which on a bus with three devices looks like a hardware fault.
struct Header {
    uint16_t len;  // payload bytes, header already subtracted
    uint8_t chan;
    uint8_t seq;
    bool cont;
};
bool parse_header(const uint8_t raw[4], Header& out) noexcept;

// Q-point fixed point: the hub sends int16 and the report's scale is a power of two.
// Gravity is Q8 in m/s^2, which is where the dial's "which way is up" comes from.
constexpr float from_q(int16_t raw, int q) noexcept {
    return static_cast<float>(raw) / static_cast<float>(1u << q);
}

// The board fact, in one function.  §6.5.1 item 4a: whatever permutation and sign flips the
// mounting needs happen ONCE, here, and `sensor imu read` on the bench is how they are
// confirmed -- upright reads up=0, on its right-hand face up=270.
void to_dial_axes(float sx, float sy, float sz, float& gx, float& gy, float& gz) noexcept;

// ---- the driver -------------------------------------------------------------------------

// Runs the boot handshake: drain the advertisement, ask for the product ID, enable
// SH2_GRAVITY and SH2_TAP_DETECTOR.  Idempotent, and called for you by read().  Budget a few
// hundred ms on a cold hub (§6.5.1 item 3).
Status init() noexcept;

// Drains every SHTP packet the hub has waiting, then answers with the latest gravity and the
// running tap count.  Draining on read is what makes this poll-driven and interrupt-free:
// a zero-length header IS "nothing pending", so no GPIO is required to find that out.
Result<imu::State> read() noexcept;

// R-BOARD-3's remedy.  Nothing above the HAL branches on it; `sensor imu` prints it.
imu::Link link() noexcept;

// Called from the SENSOR_INT ISR (IO42, active-low, push-pull).  Counts edges and nothing
// else -- the drain happens in read(), on a task, where a clock-stretching device is allowed
// to take milliseconds.  Safe to call from an ISR: one relaxed increment, no allocation, no
// logging, no I2C.
void isr_tick() noexcept;

void forget() noexcept;

}  // namespace clk::hal::bno085
