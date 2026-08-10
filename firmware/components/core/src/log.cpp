#include "clk/log.hpp"

#include <cstring>

namespace clk::log {
namespace {

// Order must match enum Mod exactly.  The static_assert below is the guard.
constexpr const char* kModNames[] = {
    "sys", "cli", "cmd", "trace", "sim",
    "idf",
    "motion", "audio", "storage", "chrono", "board", "ui", "net", "sup",
    "drv.step", "drv.opto", "drv.led", "drv.amp", "drv.exp",
    "drv.imu", "drv.als", "drv.env", "drv.sd", "drv.chg",
};
static_assert(std::size(kModNames) == kModCount, "kModNames out of sync with enum Mod");

// Index by Level.  "off" first so a prefix search finds it.
constexpr const char* kLevelNames[] = { "off", "error", "warn", "info", "debug", "verbose" };
static_assert(std::size(kLevelNames) == static_cast<std::size_t>(Level::Verbose) + 1);

constexpr bool starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

}  // namespace

const char* name(Mod m) noexcept {
    const auto i = static_cast<std::size_t>(m);
    return i < kModCount ? kModNames[i] : "?";
}

const char* name(Level l) noexcept {
    const auto i = static_cast<std::size_t>(l);
    return i < std::size(kLevelNames) ? kLevelNames[i] : "?";
}

void init(Level dflt) noexcept {
    for (auto& a : g_level) a.store(static_cast<uint8_t>(dflt), std::memory_order_relaxed);
    // Drivers are quiet by default: on a healthy board they have nothing to say, and during
    // bring-up you turn on exactly the one you are probing (`sys debug drv.opto verbose`).
    for (auto m = static_cast<std::size_t>(Mod::drv_step); m < kModCount; ++m) {
        g_level[m].store(static_cast<uint8_t>(Level::Off), std::memory_order_relaxed);
    }
    g_level[static_cast<std::size_t>(Mod::trace)].store(static_cast<uint8_t>(Level::Off),
                                                        std::memory_order_relaxed);
    g_level[static_cast<std::size_t>(Mod::sim)].store(static_cast<uint8_t>(Level::Off),
                                                       std::memory_order_relaxed);
}

void set(Mod m, Level l) noexcept {
    if (static_cast<std::size_t>(m) >= kModCount) return;
    g_level[static_cast<std::size_t>(m)].store(static_cast<uint8_t>(l), std::memory_order_relaxed);
    detail::on_level_set(m, l);
}

Level get(Mod m) noexcept {
    if (static_cast<std::size_t>(m) >= kModCount) return Level::Off;
    return static_cast<Level>(g_level[static_cast<std::size_t>(m)].load(std::memory_order_relaxed));
}

bool parseMod(std::string_view s, Mod& out) noexcept {
    for (std::size_t i = 0; i < kModCount; ++i) {
        if (s == kModNames[i]) { out = static_cast<Mod>(i); return true; }
    }
    return false;
}

bool parseLevel(std::string_view s, Level& out) noexcept {
    if (s.empty()) return false;
    int hit = -1;
    for (std::size_t i = 0; i < std::size(kLevelNames); ++i) {
        if (starts_with(kLevelNames[i], s)) {
            if (hit >= 0) return false;          // ambiguous prefix
            hit = static_cast<int>(i);
        }
    }
    if (hit < 0) return false;
    out = static_cast<Level>(hit);
    return true;
}

int setGlob(std::string_view pat, Level l) noexcept {
    int n = 0;
    if (pat == "all" || pat == "*") {
        for (std::size_t i = 0; i < kModCount; ++i) { set(static_cast<Mod>(i), l); ++n; }
        return n;
    }
    if (!pat.empty() && pat.back() == '*') {
        const auto prefix = pat.substr(0, pat.size() - 1);
        for (std::size_t i = 0; i < kModCount; ++i) {
            if (starts_with(kModNames[i], prefix)) { set(static_cast<Mod>(i), l); ++n; }
        }
        return n;
    }
    Mod m{};
    if (parseMod(pat, m)) { set(m, l); return 1; }
    return 0;
}

void write(Mod m, Level l, const char* fmt, ...) noexcept {
    std::va_list ap;
    va_start(ap, fmt);
    vwrite(m, l, fmt, ap);
    va_end(ap);
}

}  // namespace clk::log
