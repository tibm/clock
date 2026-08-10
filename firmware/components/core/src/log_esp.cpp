// ESP32 backend for clk::log.                                 [FIRMWARE.md §9.4]
//
// Goes through esp_log_write so `idf.py monitor` colouring, timestamps and the coredump
// text section behave exactly as for IDF's own logging.  Note esp_log_write does NOT
// re-check the level -- the check lives in the ESP_LOGx macro, which we do not use.  Our
// own O(1) table (clk::log::on) is the only gate, which is the point of D12.
#include <cinttypes>
#include <cstdarg>
#include <cstdio>

#include "esp_log.h"

#include "clk/log.hpp"

namespace clk::log {
namespace {

constexpr char kLetter[] = { ' ', 'E', 'W', 'I', 'D', 'V' };

constexpr esp_log_level_t kEspLevel[] = {
    ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE,
};

}  // namespace

void vwrite(Mod m, Level l, const char* fmt, std::va_list ap) noexcept {
    const auto i = static_cast<std::size_t>(l);
    // One esp_log_write call per line: the body is rendered first so the prefix and the
    // message cannot be split by a higher-priority task mid-line.
    char body[192];
    std::vsnprintf(body, sizeof body, fmt, ap);
    esp_log_write(kEspLevel[i], name(m), "%c (%" PRIu32 ") %s: %s\n",
                  kLetter[i], esp_log_timestamp(), name(m), body);
}

namespace detail {

void on_level_set(Mod m, Level l) noexcept {
    // The `idf` pseudo-module is the escape hatch for Wi-Fi / NimBLE / IDF-internal tags.
    if (m == Mod::idf) {
        esp_log_level_set("*", kEspLevel[static_cast<std::size_t>(l)]);
    }
}

}  // namespace detail
}  // namespace clk::log
