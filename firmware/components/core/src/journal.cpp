#include "clk/journal.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "clk/port.hpp"

namespace clk::journal {
namespace {

constexpr uint32_t kMagic = 0x4A524E4C;  // "JRNL"

// Function-local: a log line can come from another static constructor, and a Mutex built
// at namespace scope might not exist yet when it does.
port::Mutex& mx() noexcept {
    static port::Mutex m;
    return m;
}

// Bounded, so a producer never queues behind a stuck consumer for long -- and never forever.
// A line that cannot get the lock in time is counted, not waited for.
constexpr uint32_t kLockMs = 20;
std::atomic<uint32_t> g_busy_lost{0};
uint32_t g_carried = 0;
bool g_warm = false;
char g_line[kLineMax];  // vwrite's scratch, under mx()

bool valid(detail::Mem const& m) noexcept {
    const uint32_t used = m.head - m.tail;
    return m.magic == kMagic && used <= kBytes && static_cast<int32_t>(m.boot_at - m.tail) >= 0 &&
           static_cast<int32_t>(m.head - m.boot_at) >= 0;
}

void reset(detail::Mem& m) noexcept {
    m.head = m.tail = m.boot_at = 0;
    m.lost = m.written = 0;
    m.magic = kMagic;
}

// Drop the oldest whole lines until `n` bytes fit.  n <= kLineMax < kBytes, so it ends.
void make_room(detail::Mem& m, uint32_t n) noexcept {
    while (kBytes - (m.head - m.tail) < n) {
        const uint32_t used = m.head - m.tail;
        uint32_t k = 0;
        while (k < used && m.buf[(m.tail + k) % kBytes] != '\n') ++k;
        k = k < used ? k + 1 : used;
        m.tail += k;
        m.lost += k;
    }
    if (static_cast<int32_t>(m.tail - m.boot_at) > 0) m.boot_at = m.tail;
}

void append_locked(detail::Mem& m, const char* s, std::size_t n) noexcept {
    if (!valid(m)) reset(m);
    if (n > kLineMax) n = kLineMax;
    make_room(m, static_cast<uint32_t>(n));
    const uint32_t at = m.head % kBytes;
    const std::size_t first = n < kBytes - at ? n : kBytes - at;
    std::memcpy(m.buf + at, s, first);
    std::memcpy(m.buf, s + first, n - first);
    m.head += static_cast<uint32_t>(n);
    m.written += static_cast<uint32_t>(n);
}

// "\033[0;33m" and friends: the console's, not the file's.
std::size_t strip_ansi(char* s, std::size_t n) noexcept {
    std::size_t w = 0;
    for (std::size_t r = 0; r < n; ++r) {
        if (s[r] == '\033' && r + 1 < n && s[r + 1] == '[') {
            r += 2;
            while (r < n && !((s[r] >= 'A' && s[r] <= 'Z') || (s[r] >= 'a' && s[r] <= 'z'))) ++r;
            continue;
        }
        s[w++] = s[r];
    }
    return w;
}

}  // namespace

void init() noexcept {
    {
        port::Lock lk{mx()};
        auto& m = detail::mem();
        g_warm = valid(m);
        if (g_warm) {
            g_carried = m.head - m.tail;
            m.boot_at = m.head;
            m.lost = m.written = 0;
        } else {
            g_carried = 0;
            reset(m);
        }
    }
    detail::on_init();
}

void write(const char* s, std::size_t n) noexcept {
    if (!s || !n) return;
    if (!mx().try_lock_ms(kLockMs)) {
        g_busy_lost.fetch_add(static_cast<uint32_t>(n), std::memory_order_relaxed);
        return;
    }
    append_locked(detail::mem(), s, n);
    mx().unlock();
}

void vwrite(const char* fmt, std::va_list ap) noexcept {
    if (!fmt) return;
    if (!mx().try_lock_ms(kLockMs)) {
        g_busy_lost.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const int r = std::vsnprintf(g_line, sizeof g_line, fmt, ap);
    if (r > 0) {
        std::size_t n = static_cast<std::size_t>(r) < sizeof g_line ? static_cast<std::size_t>(r)
                                                                    : sizeof g_line - 1;
        n = strip_ansi(g_line, n);
        // A line cut at kLineMax still ends in a newline, or the next one runs into it.
        if (static_cast<std::size_t>(r) >= sizeof g_line && n) g_line[n - 1] = '\n';
        append_locked(detail::mem(), g_line, n);
    }
    mx().unlock();
}

Chunk peek(char* out, std::size_t cap) noexcept {
    Chunk c{0, 0, false};
    if (!out || !cap) return c;
    port::Lock lk{mx()};
    auto& m = detail::mem();
    if (!valid(m)) return c;
    c.from = m.tail;
    c.prev = static_cast<int32_t>(m.boot_at - m.tail) > 0;
    const uint32_t avail = c.prev ? m.boot_at - m.tail : m.head - m.tail;
    const std::size_t n = avail < cap ? avail : cap;
    const uint32_t at = m.tail % kBytes;
    const std::size_t first = n < kBytes - at ? n : kBytes - at;
    std::memcpy(out, m.buf + at, first);
    std::memcpy(out + first, m.buf, n - first);
    c.n = n;
    return c;
}

void consume(Chunk const& c) noexcept {
    if (!c.n) return;
    port::Lock lk{mx()};
    auto& m = detail::mem();
    // A producer may have dropped lines past `from` meanwhile; never move the tail back.
    const uint32_t to = c.from + static_cast<uint32_t>(c.n);
    if (static_cast<int32_t>(to - m.tail) > 0 && static_cast<int32_t>(m.head - to) >= 0)
        m.tail = to;
    if (static_cast<int32_t>(m.tail - m.boot_at) > 0) m.boot_at = m.tail;
}

Stats stats() noexcept {
    port::Lock lk{mx()};
    auto const& m = detail::mem();
    const bool ok = valid(m);
    return {ok ? m.head - m.tail : 0,
            (ok ? m.lost : 0) + g_busy_lost.load(std::memory_order_relaxed), g_carried,
            ok ? m.written : 0, g_warm};
}

}  // namespace clk::journal
