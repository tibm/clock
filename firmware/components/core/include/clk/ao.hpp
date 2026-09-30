// The active object.                                            [FIRMWARE.md D1, §4.2]
//
// One mailbox, one thread, run to completion, no shared state.  That is the whole pattern,
// and it is the reason this is ~150 lines rather than a framework: QP's *idea* is what has
// value here, and an SMP port of QP's *code* is not something anybody wants to own.
//
// The tick is in SIM time, so `sim warp 60` really does make a one-minute timer fire every
// second.  Under a large warp the tick necessarily coarsens -- the poll bound is real
// milliseconds -- which is fine for everything here because every target is absolute.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "clk/event.hpp"
#include "clk/port.hpp"

namespace clk {

inline constexpr std::size_t kMailboxDepth = 16;

class ActiveObject {
public:
    struct Cfg {
        const char* name;
        int prio;
        std::size_t stack;
        uint32_t tick_ms = 0;  // 0 = no periodic tick
    };

    explicit ActiveObject(Cfg cfg) noexcept : cfg_(cfg), tick_us_(cfg.tick_ms * 1000ull) {}
    virtual ~ActiveObject() { stop(); }

    ActiveObject(ActiveObject const&) = delete;
    ActiveObject& operator=(ActiveObject const&) = delete;

    void start() noexcept;
    void stop() noexcept;  // posts Stop, then joins

    // Never blocks and never fails loudly: a full mailbox drops the event and counts it.
    // A producer that could block on a consumer is a producer that can deadlock the system,
    // and `sys stat` showing a non-zero drop count is a better failure than a stall.
    bool post(Event const& e) noexcept;

    [[nodiscard]] const char* name() const noexcept { return cfg_.name; }
    [[nodiscard]] uint32_t dropped() const noexcept { return dropped_.load(); }
    [[nodiscard]] uint32_t handled() const noexcept { return handled_.load(); }
    [[nodiscard]] bool running() const noexcept { return run_.load(); }
    // port::now_us() at the top of this AO's loop, most recent.  The loop comes round at
    // least every kPollMs, so an age of more than a few ms is a handler that has not
    // returned -- which is what the supervisor watches for, and what the TWDT resets on.
    [[nodiscard]] uint64_t alive_us() const noexcept {
        return alive_us_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool watched() const noexcept { return watched_.load(); }  // by the TWDT
    // Inside a WdtPause: blocking on purpose, not stuck.
    [[nodiscard]] bool paused() const noexcept { return paused_.load(); }

protected:
    virtual void on_start() {}
    virtual void on_event(Event const&) {}
    virtual void on_tick() {}

    void set_tick(uint32_t ms) noexcept { tick_us_.store(ms * 1000ull); }

    // For the one kind of handler that blocks longer than the TWDT on purpose: `net` running
    // a BLE-issued command, which can be a bounded 120 s `sensor ... stream`.  The task is
    // unsubscribed for the scope and re-subscribed (and fed) after, and the supervisor does
    // not call it stuck meanwhile.  This AO's thread only.
    class WdtPause {
    public:
        explicit WdtPause(ActiveObject& ao) noexcept : ao_(ao) {
            ao_.paused_.store(true);
            if (ao_.watched_.load()) port::wdt_unwatch();
        }
        ~WdtPause() {
            if (ao_.watched_.load()) ao_.watched_.store(port::wdt_watch());
            ao_.paused_.store(false);
        }
        WdtPause(WdtPause const&) = delete;
        WdtPause& operator=(WdtPause const&) = delete;

    private:
        ActiveObject& ao_;
    };

private:
    static void entry(void*) noexcept;
    void run() noexcept;
    bool pop(Event& out) noexcept;

    // Real-milliseconds poll bound.  Short enough that a sim deadline computed before a
    // `sim warp` or a `sim jump` is re-evaluated promptly afterwards.
    static constexpr uint32_t kPollMs = 1;
    // Feed the TWDT this often.  Far inside its 10 s, far above the 1 ms loop.
    static constexpr uint64_t kFeedUs = 1'000'000;

    Cfg cfg_;
    std::atomic<uint64_t> tick_us_;
    uint64_t next_tick_us_ = 0;

    port::Mutex mx_;
    port::Signal sig_;
    Event q_[kMailboxDepth];
    std::size_t head_ = 0, count_ = 0;

    std::atomic<bool> run_{false};
    std::atomic<uint32_t> dropped_{0};
    std::atomic<uint32_t> handled_{0};
    std::atomic<uint64_t> alive_us_{0};
    std::atomic<bool> watched_{false};
    std::atomic<bool> paused_{false};
    void* thread_ = nullptr;
};

}  // namespace clk
