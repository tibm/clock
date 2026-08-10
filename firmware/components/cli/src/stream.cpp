#include "clk/cli/stream.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <thread>

#include "clk/hal/hal.hpp"
#include "clk/log.hpp"

namespace clk::cli {
namespace {

constexpr std::size_t kRing = 128;
constexpr std::size_t kTextLen = 96;

struct Sample {
    uint32_t t_ms;
    char text[kTextLen];
};

// Single-producer / single-consumer ring.  Overflow drops the NEW sample and counts it --
// dropping the oldest would silently rewrite history in the middle of a scope trace.
struct Ring {
    Sample s[kRing];
    std::atomic<uint32_t> head{0};  // producer
    std::atomic<uint32_t> tail{0};  // consumer
    std::atomic<uint32_t> dropped{0};

    bool push(Sample const& v) {
        const uint32_t h = head.load(std::memory_order_relaxed);
        const uint32_t t = tail.load(std::memory_order_acquire);
        if (h - t >= kRing) {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        s[h % kRing] = v;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(Sample& out) {
        const uint32_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        out = s[t % kRing];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
};

// "mv=1362 norm=0.412" -> keys "mv,norm" / values "1362,0.412"
void split_kv(const char* text, char* keys, std::size_t kcap, char* vals, std::size_t vcap) {
    keys[0] = vals[0] = '\0';
    std::size_t kn = 0, vn = 0;
    const char* p = text;
    bool first = true;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char* eq = std::strchr(p, '=');
        const char* sp = std::strchr(p, ' ');
        if (!eq || (sp && eq > sp)) {
            p = sp ? sp : p + std::strlen(p);
            continue;
        }
        const char* end = sp ? sp : p + std::strlen(p);
        if (!first) {
            if (kn + 1 < kcap) keys[kn++] = ',';
            if (vn + 1 < vcap) vals[vn++] = ',';
        }
        for (const char* q = p; q < eq && kn + 1 < kcap; ++q) keys[kn++] = *q;
        for (const char* q = eq + 1; q < end && vn + 1 < vcap; ++q) vals[vn++] = *q;
        keys[kn] = '\0';
        vals[vn] = '\0';
        first = false;
        p = end;
    }
}

}  // namespace

Status run_stream(const char* name, SampleFn sample, StreamOpts const& o, Sink& out) {
    if (!sample || o.hz == 0 || o.hz > kMaxHz || o.secs == 0 || o.secs > kMaxSecs) {
        out.printf("bad stream args: hz 1-%u, secs 1-%" PRIu32, kMaxHz, kMaxSecs);
        return Status::BadArg;
    }

    // One probe before spawning anything: an absent sensor should cost one line, not a
    // thread and a ten-second wait (D16).
    char probe[kTextLen];
    if (const Status st = sample(probe, sizeof probe); st == Status::NotPresent) {
        out.printf("%s: not present", name);
        return Status::NotPresent;
    }

    // 200 Hz of samples interleaved with verbose logging is unreadable, so quiet the log
    // for the duration and put it back afterwards (§9.5).
    uint8_t saved[log::kModCount];
    if (!o.keep_logs) {
        for (std::size_t i = 0; i < log::kModCount; ++i) {
            saved[i] = static_cast<uint8_t>(log::get(static_cast<log::Mod>(i)));
            if (saved[i] > static_cast<uint8_t>(log::Level::Warn)) {
                log::set(static_cast<log::Mod>(i), log::Level::Warn);
            }
        }
    }

    Ring ring;
    std::atomic<bool> stop{false};
    const uint32_t period_us = 1'000'000u / o.hz;

    // Rule 12: sampling happens here, not on the console's thread.  When the AOs land this
    // becomes the owning AO's periodic timer and the ring becomes its stream sink (§6.9);
    // the drain loop below does not change.
    std::thread producer([&] {
        auto next = std::chrono::steady_clock::now();
        const auto deadline = next + std::chrono::seconds(o.secs);
        while (!stop.load(std::memory_order_relaxed) && next < deadline) {
            Sample s{};
            s.t_ms = hal::clock_::millis();
            if (sample(s.text, sizeof s.text) == Status::NotPresent) break;
            ring.push(s);
            next += std::chrono::microseconds(period_us);
            std::this_thread::sleep_until(next);
        }
        stop.store(true, std::memory_order_relaxed);
    });

    bool header_done = false;
    uint32_t printed = 0;

    auto emit = [&](Sample const& s) {
        char keys[kTextLen], vals[kTextLen];
        if (o.csv) {
            split_kv(s.text, keys, sizeof keys, vals, sizeof vals);
            if (!header_done) {
                out.printf("# t_ms,%s", keys);
                header_done = true;
            }
            out.printf("%" PRIu32 ",%s", s.t_ms, vals);
        } else {
            if (!header_done) {
                out.printf("# %-9s %s", "t_ms", "sample");
                header_done = true;
            }
            out.printf("  %-9" PRIu32 " %s", s.t_ms, s.text);
        }
        ++printed;
    };

    for (;;) {
        Sample s{};
        if (ring.pop(s)) {
            emit(s);
            continue;
        }
        if (stop.load(std::memory_order_relaxed)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    producer.join();
    for (Sample s{}; ring.pop(s);) emit(s);  // whatever landed during the join

    if (!o.keep_logs) {
        for (std::size_t i = 0; i < log::kModCount; ++i) {
            log::set(static_cast<log::Mod>(i), static_cast<log::Level>(saved[i]));
        }
    }

    const uint32_t dropped = ring.dropped.load(std::memory_order_relaxed);
    out.printf("stream ended (%" PRIu32 "s, %" PRIu32 " samples, %" PRIu32 " dropped)", o.secs,
               printed, dropped);
    return Status::Ok;
}

}  // namespace clk::cli
