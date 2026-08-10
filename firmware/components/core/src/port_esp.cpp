// ESP32 port: FreeRTOS tasks, mutexes and task notifications.  [FIRMWARE.md §2, §3.1]
//
// Not exercised until the devkit is on the bench -- the ESP HAL is still stubs -- but it
// compiles and links with the rest, which is the point of having it now rather than
// discovering the shape of the seam on day one of bring-up.
#include "clk/port.hpp"

#include <atomic>
#include <cstdlib>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace clk::port {
namespace {

uint64_t zero_clock() noexcept { return 0; }
std::atomic<ClockFn> g_clock{zero_clock};

struct ThreadImpl {
    TaskHandle_t task = nullptr;
    void (*fn)(void*) = nullptr;
    void* arg = nullptr;
};

void trampoline(void* p) {
    auto* t = static_cast<ThreadImpl*>(p);
    t->fn(t->arg);
    vTaskDelete(nullptr);
}

}  // namespace

void set_clock(ClockFn f) noexcept { g_clock.store(f ? f : zero_clock); }
uint64_t now_us() noexcept { return g_clock.load()(); }
uint32_t now_ms() noexcept { return static_cast<uint32_t>(now_us() / 1000u); }

Mutex::Mutex() noexcept : h_(xSemaphoreCreateMutex()) {}
Mutex::~Mutex() {
    if (h_) vSemaphoreDelete(static_cast<SemaphoreHandle_t>(h_));
}
void Mutex::lock() noexcept { xSemaphoreTake(static_cast<SemaphoreHandle_t>(h_), portMAX_DELAY); }
void Mutex::unlock() noexcept { xSemaphoreGive(static_cast<SemaphoreHandle_t>(h_)); }

Signal::Signal() noexcept : h_(xSemaphoreCreateBinary()) {}
Signal::~Signal() {
    if (h_) vSemaphoreDelete(static_cast<SemaphoreHandle_t>(h_));
}
void Signal::notify() noexcept { xSemaphoreGive(static_cast<SemaphoreHandle_t>(h_)); }
void Signal::wait_real_ms(uint32_t ms) noexcept {
    xSemaphoreTake(static_cast<SemaphoreHandle_t>(h_), pdMS_TO_TICKS(ms));
}

bool thread_start(ThreadCfg cfg, void (*fn)(void*), void* arg, void** out) noexcept {
    // Allocated once at construction, before app_main returns -- rule 3 is about the steady
    // state, and a task that is never destroyed never frees this.
    auto* t = static_cast<ThreadImpl*>(std::calloc(1, sizeof(ThreadImpl)));
    if (!t) return false;
    t->fn = fn;
    t->arg = arg;
    const BaseType_t ok = xTaskCreatePinnedToCore(
        trampoline, cfg.name, static_cast<uint32_t>(cfg.stack), t,
        static_cast<UBaseType_t>(cfg.prio), &t->task, static_cast<BaseType_t>(cfg.core));
    if (ok != pdPASS) {
        std::free(t);
        return false;
    }
    *out = t;
    return true;
}

void thread_join(void* handle) noexcept {
    // An AO's task runs until reset on target; this only reclaims the handle so a host test
    // and the firmware can share one shutdown path.
    std::free(handle);
}

}  // namespace clk::port
