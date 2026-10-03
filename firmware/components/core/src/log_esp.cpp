// ESP32 backend for clk::log.                                 [FIRMWARE.md §9.4]
//
// Goes through esp_log_write so `idf.py monitor` colouring, timestamps and the coredump
// text section behave exactly as for IDF's own logging.  Note esp_log_write does NOT
// re-check the level -- the check lives in the ESP_LOGx macro, which we do not use.  Our
// own O(1) table (clk::log::on) is the only gate, which is the point of D12.
#include <cinttypes>
#include <cstdarg>
#include <cstdio>

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "clk/evtrace.hpp"
#include "clk/journal.hpp"
#include "clk/log.hpp"

namespace clk::log {
namespace {

constexpr char kLetter[] = {' ', 'E', 'W', 'I', 'D', 'V'};

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
    esp_log_write(kEspLevel[i], name(m), "%c (%" PRIu32 ") %s: %s\n", kLetter[i],
                  esp_log_timestamp(), name(m), body);
    if (const auto t = g_tap.load(std::memory_order_relaxed)) t(m, l, body);
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

// ---- the journal's memory and its tap on IDF's output -----------------------------------
namespace clk::journal::detail {
namespace {

// .noinit: not zeroed at startup, so a panic / task-watchdog / esp_restart reset leaves the
// last 16 KB of log where the next boot can pick it up (journal.hpp).  Internal DRAM.
__NOINIT_ATTR Mem g_mem;

vprintf_like_t g_prev = nullptr;

// Every line esp_log prints comes through here -- ours (log::vwrite goes through
// esp_log_write) and IDF's own: Wi-Fi, NimBLE, the SD driver, task_wdt.  esp_log calls its
// vprintf even where it takes no lock of its own (an ISR, a critical section, the scheduler
// not running yet); the journal's mutex cannot be taken there, so those lines only reach the
// console.
int tap(const char* fmt, va_list ap) {
    if (xPortCanYield() && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        va_list cp;
        va_copy(cp, ap);
        journal::vwrite(fmt, cp);
        va_end(cp);
    }
    return g_prev ? g_prev(fmt, ap) : vprintf(fmt, ap);
}

}  // namespace

Mem& mem() noexcept { return g_mem; }

void on_init() noexcept {
    if (!g_prev) g_prev = esp_log_set_vprintf(&tap);
}

}  // namespace clk::journal::detail

// ---- the event tracer's memory -----------------------------------------------------------
namespace clk::evtrace::detail {
namespace {
// RTC slow memory, not initialised: survives a panic, the watchdog, esp_restart AND deep
// sleep (evtrace.hpp).  3.2 KB of the S3's 8 KB.
RTC_NOINIT_ATTR Mem g_mem;
}  // namespace
Mem& mem() noexcept { return g_mem; }
}  // namespace clk::evtrace::detail
