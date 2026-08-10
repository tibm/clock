// Host backend for clk::log -- clocksim and GoogleTest.       [FIRMWARE.md §9.4]
// Logs go to stderr so that `sensor ... stream --csv` on stdout stays pipe-clean.
#include <chrono>
#include <cstdarg>
#include <cstdio>

#include "clk/log.hpp"

namespace clk::log {
namespace {

constexpr char kLetter[] = {' ', 'E', 'W', 'I', 'D', 'V'};

// ANSI, matching esp_log's palette so eyes trained on one work on the other.
constexpr const char* kColor[] = {"", "\033[0;31m", "\033[0;33m", "\033[0;32m", "", "\033[0;37m"};

uint32_t millis() noexcept {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

}  // namespace

void vwrite(Mod m, Level l, const char* fmt, std::va_list ap) noexcept {
    const auto i = static_cast<std::size_t>(l);
    char body[192];
    std::vsnprintf(body, sizeof body, fmt, ap);
    std::fprintf(stderr, "%s%c (%u) %s: %s\033[0m\n", kColor[i], kLetter[i], millis(), name(m),
                 body);
}

namespace detail {
void on_level_set(Mod, Level) noexcept {}  // no IDF tags to forward to on the host
}  // namespace detail

}  // namespace clk::log
