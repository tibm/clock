// The three platform primitives an active object needs.        [FIRMWARE.md §2, §4.2]
//
// Declared here, defined in port_host.cpp (std::thread) or port_esp.cpp (FreeRTOS).  The
// handles are opaque on purpose: core/ must not put a FreeRTOS type or a std::thread in a
// header that services/ includes, or the layering claim in §2 stops being checkable.
//
// core/ still depends on nothing.  The monotonic clock is INJECTED rather than called,
// because on the host it has to be *sim* time -- an AO whose timers ran on wall time would
// ignore `sim warp`, and a 30-minute sunrise would take 30 minutes to watch.
#pragma once

#include <cstddef>
#include <cstdint>

namespace clk::port {

// ---- time --------------------------------------------------------------------------------
using ClockFn = uint64_t (*)() noexcept;

// Called once during hal::init(): esp_timer_get_time() on target, warped sim time on the
// host.  Until then now_us() returns 0, which is a safe answer for "nothing has started".
void set_clock(ClockFn) noexcept;
[[nodiscard]] uint64_t now_us() noexcept;
[[nodiscard]] uint32_t now_ms() noexcept;

// ---- mutual exclusion ----------------------------------------------------------------------
class Mutex {
public:
    Mutex() noexcept;
    ~Mutex();
    Mutex(Mutex const&) = delete;
    Mutex& operator=(Mutex const&) = delete;

    void lock() noexcept;
    void unlock() noexcept;

private:
    void* h_ = nullptr;
};

class Lock {
public:
    explicit Lock(Mutex& m) noexcept : m_(m) { m_.lock(); }
    ~Lock() { m_.unlock(); }
    Lock(Lock const&) = delete;
    Lock& operator=(Lock const&) = delete;

private:
    Mutex& m_;
};

// ---- "something arrived" ---------------------------------------------------------------
// Binary, auto-reset.  The wait is in REAL milliseconds and is only ever a poll bound: sim
// deadlines are re-evaluated by the waiter after every wake, which is what lets `sim warp`
// and `sim jump` change a pending deadline that has already been computed.
class Signal {
public:
    Signal() noexcept;
    ~Signal();
    Signal(Signal const&) = delete;
    Signal& operator=(Signal const&) = delete;

    void notify() noexcept;
    void wait_real_ms(uint32_t ms) noexcept;

private:
    void* h_ = nullptr;
};

// ---- threads -------------------------------------------------------------------------------
struct ThreadCfg {
    const char* name;
    int prio;           // FreeRTOS priority; ignored on the host
    std::size_t stack;  // bytes; ignored on the host
    int core = 1;       // §3.1: everything application-side is core 1
};

bool thread_start(ThreadCfg, void (*fn)(void*), void* arg, void** out) noexcept;
void thread_join(void* handle) noexcept;  // on target this only frees the handle

}  // namespace clk::port
