// Fake peripherals for clocksim and the host tests.        [FIRMWARE.md D14, §11.2]
//
// Fidelity rule (§13.9): model what the firmware LOGIC branches on, never the device's own
// physics.  So the opto has a dark/bright span and noise, because homing branches on a
// threshold -- but there is no phototransistor model, because nothing in the firmware can
// tell the difference and a wrong model is worse than no model.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/log.hpp"
#include "clk/port.hpp"

namespace clk::hal {
namespace {

// ---- calibration placeholders ----------------------------------------------------------
// Real numbers arrive at milestone 3 with a probe on the QRE1113.  They live here rather
// than in a driver so the fake and the eventual real calibration share one definition.
constexpr uint16_t kOptoDarkMv = 200;
constexpr uint16_t kOptoBrightMv = 3000;
constexpr uint16_t kVbatEmptyMv = 3300;
constexpr uint16_t kVbatFullMv = 4050;  // the LT3652 float cap, not 4.2 (README §10)

// ---- the mechanism ---------------------------------------------------------------------
// Where the hands physically sit at power-on.  Deliberately NOT zero: if the commanded
// microstep and the visible angle agreed, `motion home` would be a no-op and the homing FSM
// would be tested by nothing at all.  `sim hand h 0` when a test wants determinism.
constexpr float kHourStartDeg = 100.0f;
constexpr float kMinuteStartDeg = 250.0f;

// The index mark on each hand's underside crossing the 3.5 mm window at r = 23.5 mm, due
// north.  Half-width plus a linear edge, so the FSM sees an EDGE rather than a step -- an
// edge is what it will get on the bench, and a step would let a sloppy detector pass here
// and fail there.  Off-index the dial still returns a little light.
constexpr float kIndexHalfDeg = 1.5f;
constexpr float kIndexEdgeDeg = 0.5f;
constexpr float kOptoDarkNorm = 0.08f;

// One shaft.  Position is integrated LAZILY: p = pos + vel*(now - t0), evaluated when
// somebody asks.  No thread, exact under `sim warp`, and `sim jump` moves the hands the way
// a real 30 s of slewing would.  `pos` is fractional on purpose -- re-basing it on every
// control tick while truncating to whole microsteps would shed ~0.5 ustep per tick, which at
// a 100 Hz tick and full speed is a degree per second of invented drift.
struct AxisSt {
    double pos = 0.0;      // microsteps
    double stop_at = 0.0;  // microsteps; motion ends here
    int32_t vel = 0;       // microsteps/s, signed, + = clockwise
    uint64_t t0_us = 0;
    float offset_deg = 0.0f;  // physical angle - commanded angle.  The unknown homing solves
};

struct State {
    // time
    double warp = 1.0;
    uint64_t sim_base_us = 0;
    uint64_t real_base_us = 0;
    // analog
    bool opto_auto = true;  // derive the opto from where the hands are; a write flips it off
    float opto = 0.10f;     // only consulted when opto_auto is false
    uint16_t vbat_mv = 4021;
    uint16_t noise_mv = 0;
    uint32_t rng = 0x1234'5678u;
    // knob
    int32_t count = 0;
    int32_t last_read = 0;
    // A knob being TURNED rather than teleported.  `sim knob <n> over <ms>` ramps the count
    // across sim time the way a finger does, and that matters now that the ui paces a setting
    // to what the hands can draw (§6.6d): a lump of forty counts in one poll is a spin no
    // hand ever performed, and the firmware is right to refuse most of it.
    int32_t turn_from = 0, turn_to = 0;
    uint64_t turn_t0 = 0;
    uint64_t turn_us = 0;  // 0 = nothing in flight
    uint64_t sw_until_us = 0;
    // ENC_SW is an INTERRUPT on IO17 (§3.3): the board cannot miss a closure, however brief.
    // Here the same edge is found by polling, and a press shorter than the poll period would
    // simply not exist -- which is a fake that behaves WORSE than the hardware it stands in
    // for.  It matters because the ux app sends `sim press down` and `up` on the real mouse
    // edges, and a quick click is easily under one 20 ms `ui` tick.  So a closure nobody has
    // read yet is held over for exactly one more read: seen once down, once up, every time.
    bool sw_seen = false;       // has a reader observed the current closure?
    bool sw_held_over = false;  // released before anybody looked -- owe them one `down`
    // movement
    AxisSt ax[2]{AxisSt{.offset_deg = kHourStartDeg}, AxisSt{.offset_deg = kMinuteStartDeg}};
    bool motor_on = false;
    // imu
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    uint16_t taps = 0;
    // expander -- ELECTRICAL levels, which is what the MCP23017 driver will read.  The
    // inputs are open-drain with the expander's pull-up, so idle is HIGH and asserted is
    // LOW; `radio_off` in the CLI and the UI is the logical sense of RadioOff, inverted here.
    bool exp[expander::kSigCount] = {
        false,  // SpkSd      out  amp in shutdown at boot
        false,  // StepStby   out  both TB6612 off at boot
        false,  // Boost12En  out  12 V gate closed
        true,   // RadioOff   in   toggle open = radios enabled (a broken harness fails safe)
        true,   // PdPg       in   derived from `plugged`
        true,   // Chrg       in   derived; OD, low WHILE charging
        true,   // Fault      in   OD, idle high
        true,   // AlsInt     in   OD, idle high
        false,  // FullchgEn  out
        false,  // VbatDivEn  out
        true,   // SpkFault   in   OD, idle high
        false,  // CellTest   out
    };
    // power
    bool plugged = true;
    // outputs
    pixels::Rgbw px[pixels::kCount]{};
    bool refreshed = false;
    uint8_t warm_pct = 0;
    uint8_t cool_pct = 0;
    bool spk_active = false;
    uint8_t vol_pct = 40;
};

std::mutex g_mx;
State g_st;

uint64_t real_us() noexcept {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now() - t0).count());
}

// Caller holds g_mx.
uint64_t sim_us_locked() noexcept {
    const uint64_t now = real_us();
    const uint64_t d = now - g_st.real_base_us;
    return g_st.sim_base_us + static_cast<uint64_t>(static_cast<double>(d) * g_st.warp);
}

void rebase_locked() noexcept {
    g_st.sim_base_us = sim_us_locked();
    g_st.real_base_us = real_us();
}

// xorshift32 -- deterministic, so a seeded test stream is reproducible.
int32_t noise_locked() noexcept {
    if (g_st.noise_mv == 0) return 0;
    uint32_t x = g_st.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_st.rng = x;
    const int32_t span = static_cast<int32_t>(g_st.noise_mv) * 2 + 1;
    return static_cast<int32_t>(x % static_cast<uint32_t>(span)) - g_st.noise_mv;
}

uint16_t clamp_mv(int32_t v) noexcept { return static_cast<uint16_t>(std::clamp(v, 0, 3300)); }

// ---- mechanism helpers (caller holds g_mx) ---------------------------------------------

int hand_idx(motor::Hand h) noexcept { return h == motor::Hand::Hour ? 0 : 1; }

float wrap360(double d) noexcept {
    d = std::fmod(d, 360.0);
    if (d < 0.0) d += 360.0;
    return static_cast<float>(d);
}

// ---- how the cube is sitting ------------------------------------------------------------
// The fake keeps ANGLES, because that is what a person setting up a scene wants to type and
// what the ux app's slider drags; the firmware reads GRAVITY, because that is the only thing
// a BNO085 can actually tell it.  This is the conversion, and it is the whole model:
//
//   yaw    the cube turned about the dial's own axis -- the plate the ux app draws, positive
//          clockwise as you look at it.  Every 30 degrees of it is one tick of §6.1d.
//   pitch  tipped away from vertical: 0 stands upright facing you, +90 lies on its back with
//          the dial to the ceiling.  At which point NOTHING in the dial plane points up, and
//          the dead zone is what the firmware is supposed to do about that.
//   roll   unused.  A third angle here would be a second way to say the first two.
//
// Output is in DIAL axes (hal.hpp): +X right across the face, +Y at the printed 12, +Z out
// through the glass.  Upright and unturned, gravity is straight down the face: (0, -g, 0).
void gravity_locked(float& gx, float& gy, float& gz) noexcept {
    constexpr double kG = 9.80665;
    constexpr double kRad = 3.14159265358979 / 180.0;
    const double y = g_st.yaw * kRad, p = g_st.pitch * kRad;
    gx = static_cast<float>(kG * std::cos(p) * std::sin(y));
    gy = static_cast<float>(-kG * std::cos(p) * std::cos(y));
    gz = static_cast<float>(-kG * std::sin(p));
}

double axis_pos_locked(int i) noexcept {
    const AxisSt& a = g_st.ax[i];
    if (a.vel == 0) return a.pos;
    const uint64_t now = sim_us_locked();
    const double dt_s = static_cast<double>(now - a.t0_us) / 1e6;
    const double p = a.pos + static_cast<double>(a.vel) * dt_s;
    return a.vel > 0 ? std::min(p, a.stop_at) : std::max(p, a.stop_at);
}

bool axis_moving_locked(int i) noexcept {
    const AxisSt& a = g_st.ax[i];
    if (a.vel == 0) return false;
    const double p = axis_pos_locked(i);
    return a.vel > 0 ? p < a.stop_at : p > a.stop_at;
}

// Freeze an axis where it currently is.  Every state change re-bases through here, which is
// what keeps the fractional position honest.
void axis_park_locked(int i) noexcept {
    g_st.ax[i].pos = axis_pos_locked(i);
    g_st.ax[i].vel = 0;
    g_st.ax[i].t0_us = sim_us_locked();
}

// What you see through the glass.  0 deg = 12 o'clock, clockwise positive.
float hand_deg_locked(int i) noexcept {
    return wrap360(axis_pos_locked(i) * 360.0 / motor::kUstepsPerRev + g_st.ax[i].offset_deg);
}

// Either hand's index mark over the window at 0 deg lights the QRE1113.  Note there is no
// interval search here: two ADC reads far enough apart WILL step over the window and miss
// it.  That aliasing is real (the phototransistor is continuous, the ADC is not) and it is
// the fastest way to find out that a homing sweep is running too fast.
float opto_from_hands_locked() noexcept {
    float best = 0.0f;
    for (int i = 0; i < 2; ++i) {
        float d = hand_deg_locked(i);
        if (d > 180.0f) d = 360.0f - d;
        float v = 0.0f;
        if (d <= kIndexHalfDeg) {
            v = 1.0f;
        } else if (d < kIndexHalfDeg + kIndexEdgeDeg) {
            v = 1.0f - (d - kIndexHalfDeg) / kIndexEdgeDeg;
        }
        best = std::max(best, v);
    }
    return kOptoDarkNorm + (1.0f - kOptoDarkNorm) * best;
}

float opto_norm_locked() noexcept { return g_st.opto_auto ? opto_from_hands_locked() : g_st.opto; }

// Charger state, shared by power::read() and the expander's open-drain CHRG pin.
bool charging_locked() noexcept { return g_st.plugged && g_st.vbat_mv < kVbatFullMv; }

// One truth about every expander pin, read by BOTH views of it: the named-signal surface
// (hal::expander, what the firmware uses today) and the register surface (hal::i2c, what a
// real Mcp23017 driver will use).  Two independent fakes that could disagree would be a
// simulator that fails a driver the hardware would have passed.
bool sig_level_locked(expander::Sig s) noexcept {
    switch (s) {
        // Derived rather than stored, so there is exactly one truth about being plugged in
        // and one about charging -- power::read() and this pin cannot disagree.
        case expander::Sig::PdPg:
            return g_st.plugged;
        case expander::Sig::Chrg:
            return !charging_locked();  // OD: low while charging
        case expander::Sig::StepStby:
            return g_st.motor_on;
        default:
            return g_st.exp[static_cast<std::size_t>(s)];
    }
}

// The knob count as it stands right now, mid-ramp included.  Evaluated lazily like the axes:
// no thread, exact under `sim warp`, and a ramp that has run out settles into `count`.
int32_t knob_count_locked() noexcept {
    if (g_st.turn_us == 0) return g_st.count;
    const uint64_t dt = sim_us_locked() - g_st.turn_t0;
    if (dt >= g_st.turn_us) {
        g_st.count = g_st.turn_to;
        g_st.turn_us = 0;
        return g_st.count;
    }
    const int64_t span = static_cast<int64_t>(g_st.turn_to) - g_st.turn_from;
    return g_st.turn_from + static_cast<int32_t>(span * static_cast<int64_t>(dt) /
                                                 static_cast<int64_t>(g_st.turn_us));
}

// Freeze the ramp where it is, so the next turn accumulates onto what the user has already
// been given rather than onto where the last one was heading.
void knob_settle_locked() noexcept {
    g_st.count = knob_count_locked();
    g_st.turn_us = 0;
}

// ---- persistent settings ----------------------------------------------------------------
// `key = value` lines, read whole and written whole.  There are a handful of keys and a
// laptop's page cache in front of the file, so anything cleverer would be cleverness for its
// own sake.  The path is set by the app (clocksim), never defaulted here: a test binary that
// wrote calibration into somebody's home directory would be a fake with side effects, and the
// honest answer for "no store configured" is the same NotPresent as an unfitted device.
struct Kv {
    std::string key;
    int32_t val;
};
std::string g_store_path;
std::vector<Kv> g_store;

void store_load_locked() noexcept {
    g_store.clear();
    if (g_store_path.empty()) return;
    std::FILE* f = std::fopen(g_store_path.c_str(), "r");
    if (!f) return;  // never written yet -- every key is simply absent
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        std::string k{line};
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        if (k.empty()) continue;
        g_store.push_back({k, static_cast<int32_t>(std::strtol(eq + 1, nullptr, 10))});
    }
    std::fclose(f);
}

bool store_save_locked() noexcept {
    if (g_store_path.empty()) return false;
    std::FILE* f = std::fopen(g_store_path.c_str(), "w");
    if (!f) return false;
    for (auto const& kv : g_store) std::fprintf(f, "%s = %d\n", kv.key.c_str(), kv.val);
    std::fclose(f);
    return true;
}

}  // namespace

// ============================ hal::clock_ ================================================
namespace clock_ {

uint64_t micros() noexcept {
    std::lock_guard lk{g_mx};
    return sim_us_locked();
}

uint32_t millis() noexcept { return static_cast<uint32_t>(micros() / 1000); }

// Real time on purpose: this paces the console and the stream producer, and warping it
// would make `sensor ... stream 100` sample at 6 kHz when warp is 60.
void sleep_ms(uint32_t ms) noexcept { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// The fake has no oscillator to fail, so it answers from the presence flag -- and `sim present
// xtal32k off` is then how you exercise the fallback path without a board that has a dud
// crystal on it (§13.9: model what the firmware branches on).
SlowSrc slow_src() noexcept {
    return board::present(board::Dev::Xtal32k) ? SlowSrc::Xtal32k : SlowSrc::RcSlow;
}

}  // namespace clock_

// ============================ hal::adc ===================================================
namespace adc {

Result<uint16_t> read_mv(Ch ch) noexcept {
    std::lock_guard lk{g_mx};
    switch (ch) {
        case Ch::Opto: {
            if (!board::present(board::Dev::Opto)) return Result<uint16_t>::bad(Status::NotPresent);
            const float span = static_cast<float>(kOptoBrightMv - kOptoDarkMv);
            const int32_t mv =
                static_cast<int32_t>(kOptoDarkMv + opto_norm_locked() * span) + noise_locked();
            return Result<uint16_t>::good(clamp_mv(mv));
        }
        case Ch::Vbat: {
            if (!board::present(board::Dev::Vbat)) return Result<uint16_t>::bad(Status::NotPresent);
            return Result<uint16_t>::good(
                static_cast<uint16_t>(std::clamp<int32_t>(g_st.vbat_mv + noise_locked(), 0, 5000)));
        }
    }
    return Result<uint16_t>::bad(Status::BadArg);
}

Result<float> read_opto_norm() noexcept {
    const auto mv = read_mv(Ch::Opto);
    if (!mv.ok()) return Result<float>::bad(mv.st);
    const float span = static_cast<float>(kOptoBrightMv - kOptoDarkMv);
    return Result<float>::good(
        std::clamp((static_cast<float>(mv.v) - kOptoDarkMv) / span, 0.0f, 1.0f));
}

}  // namespace adc

// ============================ hal::knob ==================================================
namespace knob {

Result<State> read() noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Knob)) return Result<State>::bad(Status::NotPresent);
    State s{};
    s.count = knob_count_locked();
    s.delta = s.count - g_st.last_read;
    bool down = sim_us_locked() < g_st.sw_until_us;
    if (!down && g_st.sw_held_over) {
        down = true;                // the closure this reader would otherwise have missed
        g_st.sw_held_over = false;  // and the next read sees it open again
    }
    if (down) g_st.sw_seen = true;
    s.sw = down;
    g_st.last_read = s.count;
    return Result<State>::good(s);
}

}  // namespace knob

// ============================ hal::motor =================================================
namespace motor {

Status enable(bool on) noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Motor)) return Status::NotPresent;
    if (on == g_st.motor_on) return Status::Ok;
    // Dropping STEP_STBY kills the coils, and dead coils do not turn a rotor.  Parking both
    // axes here is what makes `motion` do the MotorPower handshake (§6.1) honestly instead
    // of discovering on the bench that the hands never moved.
    if (!on) {
        axis_park_locked(0);
        axis_park_locked(1);
    }
    g_st.motor_on = on;
    g_st.exp[static_cast<std::size_t>(expander::Sig::StepStby)] = on;
    return Status::Ok;
}

bool enabled() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.motor_on;
}

Status run(Hand h, int32_t usteps_per_s, int32_t stop_at) noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Motor)) return Status::NotPresent;
    if (!g_st.motor_on) return Status::NotReady;  // STEP_STBY is low: the coils are dead
    const int i = hand_idx(h);
    axis_park_locked(i);
    // A velocity pointing away from the target is a caller bug, not a slow move: reject it
    // rather than run the hand into the next revolution.
    const double delta = static_cast<double>(stop_at) - g_st.ax[i].pos;
    if (usteps_per_s == 0 || (delta > 0) != (usteps_per_s > 0)) {
        if (delta != 0.0 && usteps_per_s != 0) return Status::BadArg;
        return Status::Ok;  // already there, or asked to stand still
    }
    g_st.ax[i].vel = usteps_per_s;
    g_st.ax[i].stop_at = stop_at;
    return Status::Ok;
}

Status hold(Hand h) noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Motor)) return Status::NotPresent;
    axis_park_locked(hand_idx(h));
    return Status::Ok;
}

Status adopt(Hand h, int32_t pos) noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Motor)) return Status::NotPresent;
    const int i = hand_idx(h);
    axis_park_locked(i);
    // Renaming the coordinate must not teleport the hand: the offset absorbs the change.
    // This is the one place the fake's mechanism and the firmware's bookkeeping meet.
    const double moved = g_st.ax[i].pos - static_cast<double>(pos);
    g_st.ax[i].offset_deg = wrap360(g_st.ax[i].offset_deg + moved * 360.0 / kUstepsPerRev);
    g_st.ax[i].pos = pos;
    g_st.ax[i].stop_at = pos;
    return Status::Ok;
}

Axis state(Hand h) noexcept {
    std::lock_guard lk{g_mx};
    const int i = hand_idx(h);
    Axis a{};
    a.pos = static_cast<int32_t>(std::llround(axis_pos_locked(i)));
    a.moving = axis_moving_locked(i);
    a.vel = a.moving ? g_st.ax[i].vel : 0;
    return a;
}

}  // namespace motor

// ============================ hal::pixels ================================================
namespace pixels {

Status set(std::size_t i, Rgbw c) noexcept {
    if (i >= kCount) return Status::BadArg;
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Pixels)) return Status::NotPresent;
    g_st.px[i] = c;
    g_st.refreshed = false;
    return Status::Ok;
}

Status set_all(Rgbw c) noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Pixels)) return Status::NotPresent;
    for (auto& p : g_st.px) p = c;
    g_st.refreshed = false;
    return Status::Ok;
}

Status refresh() noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Pixels)) return Status::NotPresent;
    g_st.refreshed = true;
    return Status::Ok;
}

Rgbw get(std::size_t i) noexcept {
    std::lock_guard lk{g_mx};
    return i < kCount ? g_st.px[i] : Rgbw{};
}

}  // namespace pixels

// ============================ hal::wake ==================================================
namespace wake {

Status set(uint8_t warm_pct, uint8_t cool_pct) noexcept {
    if (warm_pct > 100 || cool_pct > 100) return Status::BadArg;
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::WakeLed)) return Status::NotPresent;
    // §6.8 interlock 4: the 12 V boost is plugged-only, so the wake light must be gated
    // off on battery.  The fake enforces it, so a service that forgets fails here first.
    if (!g_st.plugged && (warm_pct || cool_pct)) return Status::Denied;
    g_st.warm_pct = warm_pct;
    g_st.cool_pct = cool_pct;
    return Status::Ok;
}

uint8_t warm() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.warm_pct;
}
uint8_t cool() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.cool_pct;
}

}  // namespace wake

// ============================ hal::i2c ===================================================
namespace i2c {
namespace {
struct Slave {
    uint8_t addr;
    board::Dev dev;
};
constexpr Slave kBus[] = {
    {0x20, board::Dev::Expander}, {0x29, board::Dev::Als}, {0x4A, board::Dev::Imu},
    {0x6C, board::Dev::Amp},      {0x77, board::Dev::Env},
};
}  // namespace

Result<std::size_t> scan(uint8_t* out, std::size_t cap) noexcept {
    std::size_t n = 0;
    for (auto const& s : kBus) {
        if (!board::present(s.dev)) continue;
        if (n < cap) out[n] = s.addr;
        ++n;
    }
    return Result<std::size_t>::good(n);
}

// ---- the MCP23017 at 0x20, at register level (§11.2, §13.9 item 9) ----------------------
// Every other address on kBus still answers 0 to any register, which is enough to model
// "something ACKs there" for a scan and nothing more.  The expander is different because it
// is the one device a driver is about to be written against, and a driver is exactly what a
// register file catches: BANK ordering, IODIR polarity, GPPU on GPB3 (R-BOARD-4), the
// OLAT/GPIO distinction.  Modelled: the registers the firmware branches on.  Not modelled:
// interrupt-on-change, sequential-address auto-increment, IOCON.BANK=1.
namespace {

constexpr uint8_t kMcpAddr = 0x20;
enum Reg : uint8_t {
    IODIRA = 0x00,
    IODIRB = 0x01,
    GPPUA = 0x0C,
    GPPUB = 0x0D,
    GPIOA = 0x12,
    GPIOB = 0x13,
    OLATA = 0x14,
    OLATB = 0x15,
    kRegCount = 0x16,
};

// Sig order IS the pin order (hal.hpp): 0-3 are GPA0-3, 4-11 are GPB0-7.  The static_assert
// is the guard -- reorder the enum and this stops compiling rather than silently swapping
// SPK_SD for CELL_TEST on a bench where both are one bit in a hex byte.
constexpr std::size_t kPortASigs = 4;
static_assert(static_cast<std::size_t>(expander::Sig::RadioOff) == 3);
static_assert(static_cast<std::size_t>(expander::Sig::PdPg) == kPortASigs);
static_assert(expander::kSigCount == kPortASigs + 8);

uint8_t g_mcp[kRegCount] = {0xFF, 0xFF};  // POR: both IODIR all-inputs, everything else 0

// The live pin levels as one byte per port, so a GPIO read answers what the pins are doing
// and not what was last written to them.
uint8_t port_level_locked(bool port_b) noexcept {
    uint8_t v = 0;
    const std::size_t first = port_b ? kPortASigs : 0;
    const std::size_t n = port_b ? 8 : kPortASigs;
    for (std::size_t i = 0; i < n; ++i) {
        if (sig_level_locked(static_cast<expander::Sig>(first + i)))
            v |= static_cast<uint8_t>(1u << i);
    }
    return v;
}

// A write reaches a pin only where IODIR says the pin is an output -- which is what makes
// "the driver forgot to clear IODIR" a visible failure here instead of on the bench.
void port_drive_locked(bool port_b, uint8_t val) noexcept {
    const uint8_t dir = g_mcp[port_b ? IODIRB : IODIRA];
    const std::size_t first = port_b ? kPortASigs : 0;
    const std::size_t n = port_b ? 8 : kPortASigs;
    for (std::size_t i = 0; i < n; ++i) {
        const auto s = static_cast<expander::Sig>(first + i);
        if ((dir & (1u << i)) || !expander::is_output(s)) continue;  // input: not ours to drive
        g_st.exp[static_cast<std::size_t>(s)] = (val & (1u << i)) != 0;
    }
}

// Called by sim::reset(): the register file is device state, and a test that inherits the
// previous test's IODIR is a test that passes for the wrong reason.
void mcp_reset_locked() noexcept {
    for (auto& r : g_mcp) r = 0;
    g_mcp[IODIRA] = g_mcp[IODIRB] = 0xFF;
}

}  // namespace

Result<uint8_t> read_reg(uint8_t addr, uint8_t reg) noexcept {
    std::lock_guard lk{g_mx};
    if (addr == kMcpAddr && board::present(board::Dev::Expander)) {
        if (reg >= kRegCount) return Result<uint8_t>::bad(Status::BadArg);
        if (reg == GPIOA || reg == GPIOB) {
            return Result<uint8_t>::good(port_level_locked(reg == GPIOB));
        }
        return Result<uint8_t>::good(g_mcp[reg]);
    }
    for (auto const& s : kBus) {
        if (s.addr == addr) {
            return board::present(s.dev) ? Result<uint8_t>::good(0)
                                         : Result<uint8_t>::bad(Status::NotPresent);
        }
    }
    return Result<uint8_t>::bad(Status::NotPresent);
}

Status write_reg(uint8_t addr, uint8_t reg, uint8_t val) noexcept {
    std::lock_guard lk{g_mx};
    if (addr == kMcpAddr && board::present(board::Dev::Expander)) {
        if (reg >= kRegCount) return Status::BadArg;
        g_mcp[reg] = val;
        // GPIO and OLAT are the same latch seen twice; writing either drives the outputs.
        if (reg == GPIOA || reg == OLATA) {
            g_mcp[GPIOA] = g_mcp[OLATA] = val;
            port_drive_locked(false, val);
        } else if (reg == GPIOB || reg == OLATB) {
            g_mcp[GPIOB] = g_mcp[OLATB] = val;
            port_drive_locked(true, val);
        }
        return Status::Ok;
    }
    for (auto const& s : kBus) {
        if (s.addr == addr) return board::present(s.dev) ? Status::Ok : Status::NotPresent;
    }
    return Status::NotPresent;
}

}  // namespace i2c

// ============================ hal::imu ===================================================
namespace imu {

Result<State> read() noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Imu)) return Result<State>::bad(Status::NotPresent);
    State s{};
    s.yaw_deg = g_st.yaw;
    s.pitch_deg = g_st.pitch;
    s.roll_deg = g_st.roll;
    gravity_locked(s.gx, s.gy, s.gz);
    s.taps = g_st.taps;  // monotonic: the caller diffs, so a missed poll costs no taps
    return Result<State>::good(s);
}

}  // namespace imu

// ============================ hal::expander ==============================================
namespace expander {

Result<bool> get(Sig s) noexcept {
    std::lock_guard lk{g_mx};
    if (static_cast<std::size_t>(s) >= kSigCount) return Result<bool>::bad(Status::BadArg);
    if (!board::present(board::Dev::Expander)) return Result<bool>::bad(Status::NotPresent);
    return Result<bool>::good(sig_level_locked(s));
}

Status set(Sig s, bool level) noexcept {
    if (static_cast<std::size_t>(s) >= kSigCount) return Status::BadArg;
    // An input is the outside world's to drive.  Saying so is more useful than silently
    // accepting a write that the hardware would ignore.
    if (!is_output(s)) return Status::BadArg;
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Expander)) return Status::NotPresent;
    // §6.8 interlock 1: BOOST12_EN is never asserted unless PD_PG reads high.  The fake
    // enforces it for the same reason it enforces the wake-light gate -- a service that
    // forgets fails here, not on a bench with a 12 V rail up on battery.
    if (s == Sig::Boost12En && level && !g_st.plugged) return Status::Denied;
    g_st.exp[static_cast<std::size_t>(s)] = level;
    if (s == Sig::StepStby) g_st.motor_on = level;
    return Status::Ok;
}

}  // namespace expander

// ============================ hal::audio =================================================
namespace audio {

Status enable(bool on) noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Amp)) return Status::NotPresent;
    g_st.spk_active = on;
    g_st.exp[static_cast<std::size_t>(expander::Sig::SpkSd)] = on;
    return Status::Ok;
}

bool active() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.spk_active;
}

Status set_volume_pct(uint8_t pct) noexcept {
    if (pct > 100) return Status::BadArg;
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Amp)) return Status::NotPresent;
    g_st.vol_pct = pct;
    return Status::Ok;
}

uint8_t volume_pct() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.vol_pct;
}

}  // namespace audio

// ============================ hal::power =================================================
namespace power {

Result<State> read() noexcept {
    std::lock_guard lk{g_mx};
    if (!board::present(board::Dev::Vbat)) return Result<State>::bad(Status::NotPresent);
    State s{};
    s.vbat_mv = g_st.vbat_mv;
    const int32_t pct =
        (static_cast<int32_t>(s.vbat_mv) - kVbatEmptyMv) * 100 / (kVbatFullMv - kVbatEmptyMv);
    s.soc_pct = static_cast<uint8_t>(std::clamp(pct, 0, 100));
    s.plugged = g_st.plugged;
    s.charging = charging_locked();
    s.fault = false;
    return Result<State>::good(s);
}

}  // namespace power

// ============================ hal::store =================================================
namespace store {

Result<int32_t> get_i32(const char* key) noexcept {
    if (!key || !*key) return Result<int32_t>::bad(Status::BadArg);
    std::lock_guard lk{g_mx};
    if (g_store_path.empty()) return Result<int32_t>::bad(Status::NotPresent);
    for (auto const& kv : g_store) {
        if (kv.key == key) return Result<int32_t>::good(kv.val);
    }
    return Result<int32_t>::bad(Status::NotPresent);
}

Status set_i32(const char* key, int32_t value) noexcept {
    if (!key || !*key) return Status::BadArg;
    std::lock_guard lk{g_mx};
    if (g_store_path.empty()) return Status::NotPresent;
    bool found = false;
    for (auto& kv : g_store) {
        if (kv.key == key) {
            kv.val = value;
            found = true;
            break;
        }
    }
    if (!found) g_store.push_back({std::string{key}, value});
    return store_save_locked() ? Status::Ok : Status::Failed;
}

}  // namespace store

Status init() noexcept {
    {
        std::lock_guard lk{g_mx};
        g_st.real_base_us = real_us();
    }
    // core/ owns no clock of its own (§2).  Handing it SIM time here is what makes an AO's
    // timers obey `sim warp` -- a 30-minute sunrise pre-roll watched in 30 seconds.
    port::set_clock(&clock_::micros);
    CLK_LOGI(sys, "hal: fake peripherals up (board=%s)", board::board_name());
    return Status::Ok;
}

// ============================ hal::host -- the sim control surface ========================
namespace host {

void set_warp(double f) noexcept {
    std::lock_guard lk{g_mx};
    rebase_locked();
    g_st.warp = std::clamp(f, 0.01, 10000.0);
}
double warp() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.warp;
}

void advance(uint64_t us) noexcept {
    std::lock_guard lk{g_mx};
    rebase_locked();
    g_st.sim_base_us += us;
}

void set_opto(float n) noexcept {
    std::lock_guard lk{g_mx};
    g_st.opto = std::clamp(n, 0.0f, 1.0f);
    g_st.opto_auto = false;  // an explicit value wins until `sim opto auto`
}
float opto() noexcept {
    std::lock_guard lk{g_mx};
    return opto_norm_locked();
}
void set_opto_auto(bool on) noexcept {
    std::lock_guard lk{g_mx};
    g_st.opto_auto = on;
}
bool opto_auto() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.opto_auto;
}

void set_vbat_mv(uint16_t mv) noexcept {
    std::lock_guard lk{g_mx};
    g_st.vbat_mv = mv;
}
void set_noise_mv(uint16_t p) noexcept {
    std::lock_guard lk{g_mx};
    g_st.noise_mv = p;
}
void set_seed(uint32_t s) noexcept {
    std::lock_guard lk{g_mx};
    g_st.rng = s ? s : 1;
}

void turn(int32_t detents) noexcept {
    std::lock_guard lk{g_mx};
    knob_settle_locked();
    g_st.count += detents * 4;
}

void turn_counts(int32_t counts) noexcept {
    std::lock_guard lk{g_mx};
    knob_settle_locked();
    g_st.count += counts;
}

// The same counts, delivered at a RATE.  A finger cannot put forty counts into one 20 ms
// poll, and since the ui paces a setting to what the hands can draw (§6.6d) the difference is
// no longer cosmetic: a lump is a spin the movement is right to refuse most of, and a ramp is
// a spin it can follow.  Anything already in flight settles first, so two overlapping turns
// add up the way two pushes of the same knob would.
void turn_counts_over(int32_t counts, uint32_t ms) noexcept {
    std::lock_guard lk{g_mx};
    knob_settle_locked();
    if (ms == 0) {
        g_st.count += counts;
        return;
    }
    g_st.turn_from = g_st.count;
    g_st.turn_to = g_st.count + counts;
    g_st.turn_t0 = sim_us_locked();
    g_st.turn_us = static_cast<uint64_t>(ms) * 1000u;
}

void press(uint32_t hold_ms) noexcept {
    std::lock_guard lk{g_mx};
    const uint64_t now = sim_us_locked();
    if (hold_ms == 0) {
        // A release: if nobody has read the closure yet, owe them one before it opens.
        if (now < g_st.sw_until_us && !g_st.sw_seen) g_st.sw_held_over = true;
    } else {
        g_st.sw_seen = false;
        g_st.sw_held_over = false;
    }
    g_st.sw_until_us = now + static_cast<uint64_t>(hold_ms) * 1000u;
}

void set_hand_angle(motor::Hand h, float deg) noexcept {
    std::lock_guard lk{g_mx};
    const int i = hand_idx(h);
    g_st.ax[i].offset_deg = wrap360(deg - axis_pos_locked(i) * 360.0 / motor::kUstepsPerRev);
}
void set_hand_offset(motor::Hand h, float deg) noexcept {
    std::lock_guard lk{g_mx};
    g_st.ax[hand_idx(h)].offset_deg = wrap360(deg);
}
float hand_offset(motor::Hand h) noexcept {
    std::lock_guard lk{g_mx};
    return g_st.ax[hand_idx(h)].offset_deg;
}
float hand_angle(motor::Hand h) noexcept {
    std::lock_guard lk{g_mx};
    return hand_deg_locked(hand_idx(h));
}

void set_store_path(const char* path) noexcept {
    std::lock_guard lk{g_mx};
    g_store_path = path ? path : "";
    store_load_locked();
}

const char* store_path() noexcept {
    std::lock_guard lk{g_mx};
    return g_store_path.c_str();
}

void set_orientation(float yaw, float pitch, float roll) noexcept {
    std::lock_guard lk{g_mx};
    g_st.yaw = wrap360(yaw);
    g_st.pitch = pitch;
    g_st.roll = roll;
}
void tap() noexcept {
    std::lock_guard lk{g_mx};
    ++g_st.taps;
}

void set_expander_in(expander::Sig s, bool level) noexcept {
    std::lock_guard lk{g_mx};
    if (static_cast<std::size_t>(s) < expander::kSigCount) {
        g_st.exp[static_cast<std::size_t>(s)] = level;
    }
}

void set_speaker(bool on) noexcept {
    std::lock_guard lk{g_mx};
    g_st.spk_active = on;
    g_st.exp[static_cast<std::size_t>(expander::Sig::SpkSd)] = on;
}

void set_plugged(bool p) noexcept {
    std::lock_guard lk{g_mx};
    g_st.plugged = p;
    // Unplugging drops the 12 V boost -- it is gated on PD_PG in hardware, and a fake that
    // let BOOST12_EN stay asserted across an unplug would be modelling a board that does not
    // exist (§6.8 interlock 1).
    if (!p) g_st.exp[static_cast<std::size_t>(expander::Sig::Boost12En)] = false;
}
bool plugged() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.plugged;
}

void render_pixels(char* out, std::size_t cap) noexcept {
    std::lock_guard lk{g_mx};
    std::size_t n = 0;
    for (std::size_t i = 0; i < pixels::kCount && n + 1 < cap; ++i) {
        const auto& p = g_st.px[i];
        char c = '.';
        const uint8_t mx = std::max({p.r, p.g, p.b, p.w});
        if (mx == 0)
            c = '.';
        else if (p.r == mx && p.g == mx && p.b == mx)
            c = 'W';
        else if (p.w == mx)
            c = 'W';
        else if (p.r == mx && p.g == mx)
            c = 'Y';
        else if (p.r == mx && p.b == mx)
            c = 'M';
        else if (p.g == mx && p.b == mx)
            c = 'C';
        else if (p.r == mx)
            c = 'R';
        else if (p.g == mx)
            c = 'G';
        else if (p.b == mx)
            c = 'B';
        else
            c = 'o';
        out[n++] = c;
    }
    out[n] = '\0';
}

bool refreshed() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.refreshed;
}

Snapshot snapshot() noexcept {
    std::lock_guard lk{g_mx};
    Snapshot s{};
    s.sim_us = sim_us_locked();
    s.warp = g_st.warp;
    for (int i = 0; i < 2; ++i) {
        s.hand_deg[i] = hand_deg_locked(i);
        s.hand_pos[i] = static_cast<int32_t>(std::llround(axis_pos_locked(i)));
        s.hand_moving[i] = axis_moving_locked(i);
        s.hand_vel[i] = s.hand_moving[i] ? g_st.ax[i].vel : 0;
    }
    s.motor_on = g_st.motor_on;
    for (std::size_t i = 0; i < pixels::kCount; ++i) s.px[i] = g_st.px[i];
    s.refreshed = g_st.refreshed;
    s.warm_pct = g_st.warm_pct;
    s.cool_pct = g_st.cool_pct;
    s.spk_active = g_st.spk_active;
    s.vol_pct = g_st.vol_pct;
    s.vbat_mv = g_st.vbat_mv;
    const int32_t pct =
        (static_cast<int32_t>(g_st.vbat_mv) - kVbatEmptyMv) * 100 / (kVbatFullMv - kVbatEmptyMv);
    s.soc_pct = static_cast<uint8_t>(std::clamp(pct, 0, 100));
    s.plugged = g_st.plugged;
    s.charging = charging_locked();
    // Raw, NOT knob::read() -- that call consumes `delta`, and a viewer that eats the ui
    // AO's deltas would be changing the thing it is watching.
    s.knob_count = knob_count_locked();
    s.knob_sw = sim_us_locked() < g_st.sw_until_us;
    s.opto = opto_norm_locked();
    s.opto_auto = g_st.opto_auto;
    s.yaw_deg = g_st.yaw;
    s.taps = g_st.taps;
    // Logical sense: the pin idles high (pulled up, toggle open) and asserts LOW.
    s.radio_off = !g_st.exp[static_cast<std::size_t>(expander::Sig::RadioOff)];
    return s;
}

// Deliberately outside g_mx: the hook does not return, so taking the lock would hand the
// next image a process whose fakes are locked by a thread that no longer exists.
RebootFn g_reboot = nullptr;

void set_reboot_hook(RebootFn f) noexcept { g_reboot = f; }

void reset() noexcept {
    std::lock_guard lk{g_mx};
    // Sim time survives.  "Fake hardware back to power-on" is not "rewind the universe":
    // time going backwards would strand every deadline an active object has already
    // computed, which is the same invariant set_warp() is careful about.
    const uint64_t keep_us = sim_us_locked();
    g_st = State{};
    g_st.sim_base_us = keep_us;
    g_st.real_base_us = real_us();
    i2c::mcp_reset_locked();
    board::reset_presence();
}

}  // namespace host

// The host half of `sys reboot`.  There is no reset controller to poke, so the app that owns
// the process supplies one -- clocksim re-execs itself.  A binary that installed no hook
// (the test runner) says so rather than pretending it restarted.
Status reboot() noexcept {
    if (!host::g_reboot) return Status::NotPresent;
    host::g_reboot();
    return Status::Failed;  // only reached if the hook could not restart us
}

}  // namespace clk::hal
