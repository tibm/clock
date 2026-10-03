// The event tracer: the last 256 events every AO dispatched.         [FIRMWARE.md §6.9, §4.2]
//
// `{ms since boot, receiving AO, event tag, the event's first 4 bytes}` -- 12 bytes each, 3 KB
// all told.  ActiveObject::run() records one per event it hands to on_event(), so the ring is
// "what was everybody doing" in the seconds before a hang, and `sys ev dump` prints it.
//
// On target the ring is RTC slow memory, not initialised at boot: it survives a panic, the
// task watchdog, `sys reboot` and deep sleep -- the next boot keeps the old entries and marks
// where it began, so a dump after a crash shows the crash.  A cold start fails the header
// check and starts empty.  Same contract as the journal (journal.hpp), one level lower: the
// journal is what the code SAID, this is what it was TOLD.
//
// Lock-free: a writer claims a slot with one fetch_add and stamps the slot's sequence last, so
// a reader can tell a slot that is mid-write (or was overwritten under it) and skip it.  Any
// task may record; ISRs do not (nothing dispatches from one).
//
// Zero IDF here: the backend supplies the memory (`detail::mem()`).
#pragma once

#include <cstddef>
#include <cstdint>

#include "clk/event.hpp"

namespace clk::evtrace {

inline constexpr std::size_t kEntries = 256;
inline constexpr std::size_t kSources = 16;  // distinct AO names the ring can label
inline constexpr std::size_t kNameMax = 8;   // a source's label, NUL included -- "storage"

// Adopt what the previous boot left, or start empty.  Once, early in app_main, before any AO
// starts.  Without it the tracer still records -- it just keeps nothing across a reset.
void init() noexcept;

// Which label `name` records under.  Idempotent: the same name gets the same id, this boot and
// (for a warm reset of the same image) the previous one.  0xFF when the table is full.
[[nodiscard]] uint8_t source(const char* name) noexcept;

// An event identical (tag and bytes) to the one this source recorded last is counted in
// `repeats` instead of taking a slot.
void record(uint8_t src, Event const&) noexcept;

// The variant's alternative names, by index -- "HandTarget", "KnobPress".
[[nodiscard]] const char* tag_name(uint8_t tag) noexcept;

struct Entry {
    uint32_t seq;  // free-running; the oldest has the smallest
    uint32_t ms;   // port::now_ms() of THAT boot
    uint8_t src;   // source() id
    uint8_t tag;   // Event::index()
    uint8_t data[4];
    bool prev;  // recorded before this boot began
};

// Oldest first, at most `cap`; returns how many were copied.  Slots caught mid-write are left
// out rather than shown half-updated.
std::size_t snapshot(Entry* out, std::size_t cap) noexcept;
// A source id's label; "?" for one never registered.
[[nodiscard]] const char* source_name(uint8_t src) noexcept;

struct Stats {
    uint32_t recorded;  // since boot
    uint32_t repeats;   // since boot: same as that AO's previous event, so not stored
    uint32_t carried;   // entries adopted from the previous boot at init()
    bool warm;          // init() found a valid ring
};
Stats stats() noexcept;
void clear() noexcept;  // empty the ring; labels are kept

namespace detail {
// Every field goes through std::atomic_ref, so the host's TSan build sees no race -- and the
// target needs no read-modify-write on RTC memory, which the S3's S32C1I does not promise.
struct Slot {
    uint32_t ms;
    uint32_t data;  // the event's first 4 bytes, as memcpy'd
    uint8_t src, tag;
    uint16_t seq16;  // low half of the slot's sequence, stored last -- the tear check
};
static_assert(sizeof(Slot) == 12);

struct Mem {
    uint32_t magic;
    uint32_t head;     // next sequence, mirrored after each record; slot = seq % kEntries
    uint32_t boot_at;  // head when this boot began
    uint32_t base;     // the oldest sequence that counts: head at the last clear()
    char names[kSources][kNameMax];
    Slot slot[kEntries];
};
// Defined by the backend: RTC slow memory on target, a plain static on the host.
Mem& mem() noexcept;
}  // namespace detail

}  // namespace clk::evtrace
