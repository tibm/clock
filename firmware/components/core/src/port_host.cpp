// Host port: std::thread and friends.                        [FIRMWARE.md §2, §11.2]
#include "clk/port.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace clk::port {
namespace {

uint64_t zero_clock() noexcept { return 0; }
std::atomic<ClockFn> g_clock{zero_clock};

struct SignalImpl {
    std::mutex mx;
    std::condition_variable cv;
    bool flag = false;
};

struct ThreadImpl {
    std::thread th;
};

}  // namespace

void set_clock(ClockFn f) noexcept { g_clock.store(f ? f : zero_clock); }
uint64_t now_us() noexcept { return g_clock.load()(); }
uint32_t now_ms() noexcept { return static_cast<uint32_t>(now_us() / 1000u); }

Mutex::Mutex() noexcept : h_(new std::mutex) {}
Mutex::~Mutex() { delete static_cast<std::mutex*>(h_); }
void Mutex::lock() noexcept { static_cast<std::mutex*>(h_)->lock(); }
void Mutex::unlock() noexcept { static_cast<std::mutex*>(h_)->unlock(); }

Signal::Signal() noexcept : h_(new SignalImpl) {}
Signal::~Signal() { delete static_cast<SignalImpl*>(h_); }

void Signal::notify() noexcept {
    auto* s = static_cast<SignalImpl*>(h_);
    {
        std::lock_guard lk{s->mx};
        s->flag = true;
    }
    s->cv.notify_one();
}

void Signal::wait_real_ms(uint32_t ms) noexcept {
    auto* s = static_cast<SignalImpl*>(h_);
    std::unique_lock lk{s->mx};
    if (!s->flag) s->cv.wait_for(lk, std::chrono::milliseconds(ms));
    s->flag = false;
}

bool thread_start(ThreadCfg, void (*fn)(void*), void* arg, void** out) noexcept {
    auto* t = new ThreadImpl;
    t->th = std::thread(fn, arg);
    *out = t;
    return true;
}

void thread_join(void* handle) noexcept {
    auto* t = static_cast<ThreadImpl*>(handle);
    if (!t) return;
    if (t->th.joinable()) t->th.join();
    delete t;
}

}  // namespace clk::port
