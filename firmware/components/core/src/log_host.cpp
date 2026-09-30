// Host backend for clk::log -- clocksim and GoogleTest.       [FIRMWARE.md §9.4]
// Logs go to stderr so that `sensor ... stream --csv` on stdout stays pipe-clean.
#include <chrono>
#include <cstdarg>
#include <cstdio>

#include "clk/journal.hpp"
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
    const uint32_t t = millis();
    std::fprintf(stderr, "%s%c (%u) %s: %s\033[0m\n", kColor[i], kLetter[i], t, name(m), body);
    // No vprintf to hook on the host, so the line goes to the journal from here.
    char line[journal::kLineMax];
    const int n =
        std::snprintf(line, sizeof line, "%c (%u) %s: %s\n", kLetter[i], t, name(m), body);
    if (n > 0) {
        const auto len = static_cast<std::size_t>(n);
        journal::write(line, len < sizeof line ? len : sizeof line - 1);
    }
    if (const auto t = g_tap.load(std::memory_order_relaxed)) t(m, l, body);
}

namespace detail {
void on_level_set(Mod, Level) noexcept {}  // no IDF tags to forward to on the host
}  // namespace detail

}  // namespace clk::log

namespace clk::journal::detail {
Mem& mem() noexcept {
    static Mem m{};  // zeroed: the first write finds no magic and starts it empty
    return m;
}
void on_init() noexcept {}
}  // namespace clk::journal::detail
