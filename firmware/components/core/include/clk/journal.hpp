// The debug journal: every log line, in a RAM ring that survives a reset.  [FIRMWARE.md §9.4a]
//
// `storage` drains it to one text file per boot on the card (`/sd/debug/<boot>.log`), every
// few seconds.  What the ring holds when the chip resets -- a panic, the task watchdog, a
// `sys reboot` -- is still there on the next boot, because on target the ring sits in
// `.noinit` RAM, which a warm reset does not clear.  The next boot hands those bytes to
// `storage` first, marked as the PREVIOUS boot's, and they go to the end of that boot's
// file: the last seconds before a hang are the ones worth having, and they are exactly the
// ones that never made it to the card.
//
// A cold start (power-on, brown-out) finds garbage, fails the header check, and starts empty.
//
// Producers are any task -- never an ISR, and never with the scheduler stopped: on target the
// log backend checks that before it gets here.  One consumer (`storage`).  When the ring is
// full the OLDEST whole lines go and are counted in `lost`; a producer never waits for the
// card.
//
// Zero IDF here, like log.hpp: the backend supplies the memory (`detail::mem()`).
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace clk::journal {

inline constexpr std::size_t kBytes = 16384;
inline constexpr std::size_t kLineMax = 256;  // one write is cut here

// Adopt what the previous boot left (a warm reset) or start empty.  Once, first thing in
// app_main.  Without it the journal still works -- it just carries nothing across a reset.
void init() noexcept;

// Append raw bytes.  Text only; the caller supplies the newline.
void write(const char* s, std::size_t n) noexcept;
// Format and append, ANSI colour codes stripped.  The scratch line is static (under the
// journal's lock), so this costs the caller's stack nothing beyond vsnprintf itself -- which
// matters, because on target it runs on every task that logs, IDF's own included.
void vwrite(const char* fmt, std::va_list) noexcept;

// The consumer's side: look, write it somewhere, then consume what was written.  A chunk is
// either all the previous boot's or all this boot's (`prev`), never a mix.
struct Chunk {
    std::size_t n;  // bytes copied into `out`; 0 = nothing waiting
    uint32_t from;  // pass back to consume()
    bool prev;      // these bytes belong to the boot before this one
};
Chunk peek(char* out, std::size_t cap) noexcept;
void consume(Chunk const&) noexcept;

struct Stats {
    uint32_t used;     // bytes waiting
    uint32_t lost;     // bytes dropped (oldest lines) since boot because nobody drained
    uint32_t carried;  // bytes adopted from the previous boot at init()
    uint32_t written;  // bytes appended since boot
    bool warm;         // init() found a valid ring
};
Stats stats() noexcept;

namespace detail {
struct Mem {
    uint32_t magic;
    uint32_t head, tail;  // free-running byte counts; used = head - tail
    uint32_t boot_at;     // head when this boot began: [tail, boot_at) is the previous boot's
    uint32_t lost, written;
    char buf[kBytes];
};
// Defined by the backend: `.noinit` on target, a plain static on the host.
Mem& mem() noexcept;
// Backend hook, run at the end of init(): on target it routes IDF's own log output (Wi-Fi,
// NimBLE, the SD driver) into the journal too.
void on_init() noexcept;
}  // namespace detail

}  // namespace clk::journal
