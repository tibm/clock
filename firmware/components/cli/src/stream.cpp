#include "clk/cli/stream.hpp"

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "clk/hal/hal.hpp"
#include "clk/log.hpp"
#include "clk/port.hpp"

namespace clk::cli {
namespace {

constexpr std::size_t kRing = 128;
constexpr std::size_t kTextLen = 96;

// The producer's stack.  It used to be a std::thread, which on ESP-IDF is a pthread with
// CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT -- 3 KB -- and that was fine for exactly as long as
// the samplers were cheap.  `sensor imu stream` is not cheap: bno085::read() drains SHTP
// packets through two 128-byte buffers, and the sample line is formatted with %f, which on
// xtensa pulls in a formatter that wants several hundred bytes of its own.
//
// It overflowed, and the way it presented is worth recording because none of it names a
// stack: hundreds of `task_wdt: esp_task_wdt_reset(707): task not found`, then
// `assert failed: xRingbufferSend ringbuf.c:1049 (pxRingbuffer)` -- a smashed neighbour, not
// a diagnosis.  8 KB is roughly triple the deepest sampler measured and the task exists only
// for the length of one bounded stream.  (2026-09-10.)
constexpr std::size_t kProducerStack = 8192;
constexpr int kProducerPrio = 4;  // below the console (§4), above nothing that matters here

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

    // STATIC, not a local.  sizeof(Ring) is about 12.8 KB and it used to sit on the console
    // task's stack, which is a large thing to put somewhere small for no reason -- streams are
    // bounded and hold the console until they end, so there is never a second one to collide
    // with.  The `Args` below say the same thing in the `sensor stop` help text.
    static Ring ring;
    ring.head.store(0, std::memory_order_relaxed);
    ring.tail.store(0, std::memory_order_relaxed);
    ring.dropped.store(0, std::memory_order_relaxed);

    // Everything the producer task touches, in one place: it outlives the lambda a std::thread
    // used to capture, so the state has to be explicit rather than captured by reference.
    struct Producer {
        SampleFn sample;
        Ring* ring;
        uint32_t period_ms;
        uint32_t want;  // total samples: the bound is a COUNT, see below
        std::atomic<bool> stop{false};
        std::atomic<bool> done{false};
    };
    // Bounded by a sample COUNT and paced by a fixed sleep, rather than by comparing a clock
    // against a deadline.  Two reasons, and the second is the one that matters:
    //
    //   hal::clock_::millis() is SIM time on the host while sleep_ms() is real, so a loop that
    //   paced itself by the difference would, under `sim warp 60`, find itself permanently
    //   behind and spin at the sleep floor for the whole run -- starving the active objects it
    //   shares a laptop with.  A count cannot do that whatever the clock is doing.
    //
    //   And it is honest about a slow sampler: `sensor env` blocks ~200 ms for the heater
    //   soak, so a 1 Hz 30 s stream of it takes 36 s and yields the 30 samples asked for,
    //   rather than yielding 25 and calling it 30.
    const uint32_t period_ms = 1000u / o.hz;  // kMaxHz is 200, so this is never 0
    Producer prod{sample, &ring, period_ms ? period_ms : 1, o.secs * o.hz, {}, {}};

    // Rule 12: sampling happens here, not on the console's thread.  When the AOs land this
    // becomes the owning AO's periodic timer and the ring becomes its stream sink (§6.9);
    // the drain loop below does not change.
    auto body = [](void* p) {
        auto* pr = static_cast<Producer*>(p);
        for (uint32_t n = 0; n < pr->want; ++n) {
            if (pr->stop.load(std::memory_order_relaxed)) break;
            Sample s{};
            // The TIMESTAMP stays sim time -- that is the axis every other number in this
            // system is plotted against.  It is only the PACING that must not be.
            s.t_ms = hal::clock_::millis();
            if (pr->sample(s.text, sizeof s.text) == Status::NotPresent) break;
            pr->ring->push(s);
            hal::clock_::sleep_ms(pr->period_ms);
        }
        pr->stop.store(true, std::memory_order_relaxed);
        pr->done.store(true, std::memory_order_release);
    };

    void* handle = nullptr;
    if (!port::thread_start({"clk.stream", kProducerPrio, kProducerStack, 1}, body, &prod,
                            &handle)) {
        out.line("could not start the stream producer");
        if (!o.keep_logs) {
            for (std::size_t i = 0; i < log::kModCount; ++i) {
                log::set(static_cast<log::Mod>(i), static_cast<log::Level>(saved[i]));
            }
        }
        return Status::Failed;
    }

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
        if (prod.done.load(std::memory_order_acquire)) break;
        hal::clock_::sleep_ms(2);
    }
    // Wait for the task to have actually left `body` before the Producer on this stack dies.
    // port::thread_join() only reclaims the handle on target -- it does not wait -- so the
    // `done` flag is the join, and skipping it would free `prod` under a task still writing
    // to it.
    while (!prod.done.load(std::memory_order_acquire)) hal::clock_::sleep_ms(1);
    port::thread_join(handle);
    for (Sample s{}; ring.pop(s);) emit(s);  // whatever landed at the end

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
