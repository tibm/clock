// Per-module runtime log levels.   [FIRMWARE.md D12, §9.4]
//
// Rule 9: nothing below apps/ calls printf or ESP_LOGx directly.  Only these macros.
//
//   CLK_LOGI(motion, "homed after %d us", n);
//   CLK_LOGV(drv_led, "pixel %u = %02x%02x%02x%02x", i, r, g, b, w);
//
// The enabled-check is a relaxed atomic load and a compare (~2 ns), so a CLK_LOGV may sit
// in a 20 kHz path.  The compile ceiling (CONFIG_CLOCK_LOG_MAX_LEVEL_*) discards both the
// call and the format string in release builds.
//
// Zero IDF here: core/ builds for the target, for clocksim and for host tests unchanged.
#pragma once

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace clk::log {

enum class Level : uint8_t { Off = 0, Error, Warn, Info, Debug, Verbose };

// The module list IS the AO list (D12), plus the drivers and a few cross-cutting ones.
// Keep it <= 32 so a selection fits a uint32_t mask.
enum class Mod : uint8_t {
    // cross-cutting
    sys,
    cli,
    cmd,
    trace,
    sim,
    idf,  // pseudo-module: forwards to esp_log_level_set("*") on target
    // active objects (FIRMWARE.md §3.2)
    motion,
    audio,
    storage,
    chrono,
    board,
    ui,
    net,
    sup,
    // drivers -- named "drv.xxx" so `sys debug drv.* debug` works
    drv_step,
    drv_opto,
    drv_led,
    drv_amp,
    drv_exp,
    drv_imu,
    drv_als,
    drv_env,
    drv_sd,
    drv_chg,
    count
};

inline constexpr std::size_t kModCount = static_cast<std::size_t>(Mod::count);
static_assert(kModCount <= 32, "keep the module list maskable in a uint32_t");

// ---- compile-time ceiling -------------------------------------------------------------
#if defined(CONFIG_CLOCK_LOG_MAX_LEVEL_OFF)
inline constexpr Level kCeiling = Level::Off;
#elif defined(CONFIG_CLOCK_LOG_MAX_LEVEL_ERROR)
inline constexpr Level kCeiling = Level::Error;
#elif defined(CONFIG_CLOCK_LOG_MAX_LEVEL_WARN)
inline constexpr Level kCeiling = Level::Warn;
#elif defined(CONFIG_CLOCK_LOG_MAX_LEVEL_INFO)
inline constexpr Level kCeiling = Level::Info;
#elif defined(CONFIG_CLOCK_LOG_MAX_LEVEL_DEBUG)
inline constexpr Level kCeiling = Level::Debug;
#else
inline constexpr Level kCeiling = Level::Verbose;
#endif

// ---- the level table ------------------------------------------------------------------
// One relaxed atomic per module.  Deliberately not IDF's per-tag cache (D12).
inline std::array<std::atomic<uint8_t>, kModCount> g_level{};

[[nodiscard]] inline bool on(Mod m, Level l) noexcept {
    return static_cast<uint8_t>(l) <=
           g_level[static_cast<std::size_t>(m)].load(std::memory_order_relaxed);
}

// ---- control --------------------------------------------------------------------------
void init(Level dflt = Level::Info) noexcept;  // call once, before any AO starts
void set(Mod, Level) noexcept;
Level get(Mod) noexcept;

// "all" | "*" | "drv.*" | "motion".  Returns how many modules matched, 0 = no match.
int setGlob(std::string_view pattern, Level) noexcept;

const char* name(Mod) noexcept;
const char* name(Level) noexcept;
bool parseMod(std::string_view, Mod&) noexcept;
bool parseLevel(std::string_view, Level&) noexcept;  // any unique prefix: v, i, e, w, d, o

// ---- backend seam ---------------------------------------------------------------------
// Defined by log_esp.cpp (esp_log_write) or log_host.cpp (stderr).  Never called directly.
void write(Mod, Level, const char* fmt, ...) noexcept __attribute__((format(printf, 3, 4)));
void vwrite(Mod, Level, const char* fmt, std::va_list) noexcept;

// ---- second consumer --------------------------------------------------------------------
// One extra reader of the formatted line, for a transport that is not the console --
// clocksim's UI bridge today, `sys ev` and BLE log forwarding later.  Called on the
// PRODUCING thread right after formatting, so it must not block, must not allocate and must
// not itself log.  Set once during init; nullptr disables.
using Tap = void (*)(Mod, Level, const char* text);
inline std::atomic<Tap> g_tap{nullptr};
inline void set_tap(Tap t) noexcept { g_tap.store(t, std::memory_order_relaxed); }

namespace detail {
// Backend hook, so `sys debug idf warn` can reach esp_log_level_set("*") without core/
// including any IDF header.
void on_level_set(Mod, Level) noexcept;
}  // namespace detail

}  // namespace clk::log

// ---- macros ---------------------------------------------------------------------------
#define CLK_LOG_AT(lvl_, mod_, ...)                                                             \
    do {                                                                                        \
        if constexpr (::clk::log::Level::lvl_ <= ::clk::log::kCeiling) {                        \
            if (::clk::log::on(::clk::log::Mod::mod_, ::clk::log::Level::lvl_)) {               \
                ::clk::log::write(::clk::log::Mod::mod_, ::clk::log::Level::lvl_, __VA_ARGS__); \
            }                                                                                   \
        }                                                                                       \
    } while (0)

#define CLK_LOGE(mod, ...) CLK_LOG_AT(Error, mod, __VA_ARGS__)
#define CLK_LOGW(mod, ...) CLK_LOG_AT(Warn, mod, __VA_ARGS__)
#define CLK_LOGI(mod, ...) CLK_LOG_AT(Info, mod, __VA_ARGS__)
#define CLK_LOGD(mod, ...) CLK_LOG_AT(Debug, mod, __VA_ARGS__)
#define CLK_LOGV(mod, ...) CLK_LOG_AT(Verbose, mod, __VA_ARGS__)
