#include "clk/ao.hpp"

#include "clk/evtrace.hpp"

namespace clk {

void ActiveObject::start() noexcept {
    if (run_.exchange(true)) return;
    trace_id_ = evtrace::source(cfg_.name);
    next_tick_us_ = port::now_us() + tick_us_.load();
    port::thread_start({cfg_.name, cfg_.prio, cfg_.stack, 1}, &ActiveObject::entry, this, &thread_);
}

void ActiveObject::stop() noexcept {
    if (!run_.exchange(false)) return;
    post(Stop{});
    sig_.notify();
    port::thread_join(thread_);
    thread_ = nullptr;
    // The Stop above is still queued if the thread saw run_ go false first.  Left there, a
    // later start() pops it and the new thread exits at once -- running() true, never looping
    // again.  (Found by the supervisor's stuck-AO check, 2026-09-29.)
    port::Lock lk{mx_};
    head_ = count_ = 0;
}

bool ActiveObject::post(Event const& e) noexcept {
    {
        port::Lock lk{mx_};
        if (count_ == kMailboxDepth) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        q_[(head_ + count_) % kMailboxDepth] = e;
        ++count_;
    }
    sig_.notify();
    return true;
}

bool ActiveObject::pop(Event& out) noexcept {
    port::Lock lk{mx_};
    if (count_ == 0) return false;
    out = q_[head_];
    head_ = (head_ + 1) % kMailboxDepth;
    --count_;
    return true;
}

void ActiveObject::entry(void* self) noexcept { static_cast<ActiveObject*>(self)->run(); }

void ActiveObject::run() noexcept {
    on_start();
    // Watched from here on, not across on_start(): that is where the one-off slow things are
    // (mounting a card, reading NVS), and a watchdog reset during boot would be a boot loop.
    watched_.store(port::wdt_watch());
    uint64_t fed_us = port::now_us();
    while (run_.load(std::memory_order_relaxed)) {
        const uint64_t loop_us = port::now_us();
        alive_us_.store(loop_us, std::memory_order_relaxed);
        // A handler that never returns stops this, and the TWDT resets the chip.  Keyed on
        // this loop and nothing else: an AO that is merely idle still comes round every 1 ms.
        if (loop_us - fed_us >= kFeedUs || loop_us < fed_us) {
            port::wdt_feed();
            fed_us = loop_us;
        }
        Event ev;
        if (pop(ev)) {
            if (as<Stop>(ev)) break;
            // Before the handler: one that never returns is then the ring's last line.
            evtrace::record(trace_id_, ev);
            on_event(ev);
            handled_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        const uint64_t tick = tick_us_.load(std::memory_order_relaxed);
        if (tick) {
            const uint64_t now = port::now_us();
            // The second clause is the belt: if the clock ever moves backwards under us --
            // a rewound simulation, a re-based RTC -- a deadline computed before the move
            // would sit unreachably in the future and the AO would go silent forever.
            if (now >= next_tick_us_ || next_tick_us_ - now > tick) {
                // Re-based, not accumulated: after a `sim jump 300` the answer is one tick,
                // not five thousand queued ones.
                next_tick_us_ = now + tick;
                on_tick();
                continue;
            }
        }
        sig_.wait_real_ms(kPollMs);
    }
    if (watched_.exchange(false)) port::wdt_unwatch();
}

}  // namespace clk
