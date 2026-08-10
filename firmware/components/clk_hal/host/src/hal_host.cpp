// Fake peripherals for clocksim and the host tests.        [FIRMWARE.md D14, §11.2]
//
// Fidelity rule (§13.9): model what the firmware LOGIC branches on, never the device's own
// physics.  So the opto has a dark/bright span and noise, because homing branches on a
// threshold -- but there is no phototransistor model, because nothing in the firmware can
// tell the difference and a wrong model is worse than no model.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#include "clk/board.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/log.hpp"

namespace clk::hal {
namespace {

// ---- calibration placeholders ----------------------------------------------------------
// Real numbers arrive at milestone 3 with a probe on the QRE1113.  They live here rather
// than in a driver so the fake and the eventual real calibration share one definition.
constexpr uint16_t kOptoDarkMv = 200;
constexpr uint16_t kOptoBrightMv = 3000;
constexpr uint16_t kVbatEmptyMv = 3300;
constexpr uint16_t kVbatFullMv = 4050;  // the LT3652 float cap, not 4.2 (README §10)

struct State {
    // time
    double warp = 1.0;
    uint64_t sim_base_us = 0;
    uint64_t real_base_us = 0;
    // analog
    float opto = 0.10f;
    uint16_t vbat_mv = 4021;
    uint16_t noise_mv = 0;
    uint32_t rng = 0x1234'5678u;
    // knob
    int32_t count = 0;
    int32_t last_read = 0;
    uint64_t sw_until_us = 0;
    // power
    bool plugged = true;
    // outputs
    pixels::Rgbw px[pixels::kCount]{};
    bool refreshed = false;
    uint8_t warm_pct = 0;
    uint8_t cool_pct = 0;
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
                static_cast<int32_t>(kOptoDarkMv + g_st.opto * span) + noise_locked();
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
    s.count = g_st.count;
    s.delta = g_st.count - g_st.last_read;
    s.sw = sim_us_locked() < g_st.sw_until_us;
    g_st.last_read = g_st.count;
    return Result<State>::good(s);
}

}  // namespace knob

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

Result<uint8_t> read_reg(uint8_t addr, uint8_t) noexcept {
    for (auto const& s : kBus) {
        if (s.addr == addr) {
            return board::present(s.dev) ? Result<uint8_t>::good(0)
                                         : Result<uint8_t>::bad(Status::NotPresent);
        }
    }
    return Result<uint8_t>::bad(Status::NotPresent);
}

Status write_reg(uint8_t addr, uint8_t, uint8_t) noexcept {
    for (auto const& s : kBus) {
        if (s.addr == addr) return board::present(s.dev) ? Status::Ok : Status::NotPresent;
    }
    return Status::NotPresent;
}

}  // namespace i2c

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
    s.charging = g_st.plugged && s.vbat_mv < kVbatFullMv;
    s.fault = false;
    return Result<State>::good(s);
}

}  // namespace power

Status init() noexcept {
    std::lock_guard lk{g_mx};
    g_st.real_base_us = real_us();
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
}
float opto() noexcept {
    std::lock_guard lk{g_mx};
    return g_st.opto;
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
    g_st.count += detents * 4;
}

void press(uint32_t hold_ms) noexcept {
    std::lock_guard lk{g_mx};
    g_st.sw_until_us = sim_us_locked() + static_cast<uint64_t>(hold_ms) * 1000u;
}

void set_plugged(bool p) noexcept {
    std::lock_guard lk{g_mx};
    g_st.plugged = p;
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

void reset() noexcept {
    std::lock_guard lk{g_mx};
    g_st = State{};
    g_st.real_base_us = real_us();
    board::reset_presence();
}

}  // namespace host
}  // namespace clk::hal
