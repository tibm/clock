#include "clk/ao.hpp"

namespace clk {

void ActiveObject::start() noexcept {
    if (run_.exchange(true)) return;
    next_tick_us_ = port::now_us() + tick_us_.load();
    port::thread_start({cfg_.name, cfg_.prio, cfg_.stack, 1}, &ActiveObject::entry, this, &thread_);
}

void ActiveObject::stop() noexcept {
    if (!run_.exchange(false)) return;
    post(Stop{});
    sig_.notify();
    port::thread_join(thread_);
    thread_ = nullptr;
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
    while (run_.load(std::memory_order_relaxed)) {
        Event ev;
        if (pop(ev)) {
            if (as<Stop>(ev)) break;
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
}

}  // namespace clk
