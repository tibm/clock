#include "clk/evtrace.hpp"

#include <atomic>
#include <cstring>
#include <iterator>
#include <type_traits>
#include <utility>

#include "clk/port.hpp"

namespace clk::evtrace {
namespace {

constexpr uint32_t kMagic = 0x45565452;  // "EVTR"

// The counter writers fetch_add lives in ordinary RAM; the ring's own `head` is only a mirror
// for the next boot.  See Slot.
std::atomic<uint32_t> g_head{0};
std::atomic<uint32_t> g_recorded{0};
uint32_t g_carried = 0;
bool g_warm = false;
// The last event each source recorded: a repeat of it is counted, not stored.  `motion` is
// re-told the dial tick every 500 ms whether or not it changed, which on its own would push
// everything else out of the ring in two minutes.  Bit 40 set = "something recorded".
uint64_t g_last[kSources] = {};
std::atomic<uint32_t> g_repeats{0};

port::Mutex& mx() noexcept {  // the label table only; record() never takes it
    static port::Mutex m;
    return m;
}

template <class T>
T load(T& v, std::memory_order o = std::memory_order_relaxed) noexcept {
    return std::atomic_ref<T>{v}.load(o);
}
template <class T>
void store(T& v, std::type_identity_t<T> x,
           std::memory_order o = std::memory_order_relaxed) noexcept {
    std::atomic_ref<T>{v}.store(x, o);
}

bool valid(detail::Mem const& m) noexcept {
    if (m.magic != kMagic || static_cast<int32_t>(m.head - m.boot_at) < 0 ||
        static_cast<int32_t>(m.head - m.base) < 0)
        return false;
    for (auto const& n : m.names)
        if (std::memchr(n, '\0', kNameMax) == nullptr) return false;
    return true;
}

void reset(detail::Mem& m) noexcept {
    m.head = m.boot_at = m.base = g_head.load();
    std::memset(m.names, 0, sizeof m.names);
    for (auto& s : m.slot) {
        store(s.seq16, uint16_t{0xFFFF});
        store(s.ms, 0u);
    }
    m.magic = kMagic;
}

// The first min(4, sizeof(T)) bytes of whichever alternative `e` holds.  Empty structs
// (`Tap`, `Halt`) give nothing, which is what they carry.
template <std::size_t... I>
uint32_t payload(Event const& e, std::index_sequence<I...>) noexcept {
    uint32_t v = 0;
    (
        [&] {
            if (auto const* p = std::get_if<I>(&e)) {
                using T = std::variant_alternative_t<I, Event>;
                if constexpr (!std::is_empty_v<T>)
                    std::memcpy(&v, p, sizeof(T) < sizeof v ? sizeof(T) : sizeof v);
            }
        }(),
        ...);
    return v;
}

constexpr const char* kTags[] = {
    "none",       "HandTarget", "HomeRequest", "ZeroSet", "DialTick", "Halt",     "HomeDone",
    "HandState",  "KnobDelta",  "KnobPress",   "Tap",     "ModeSet",  "AlarmCfg", "TimeChanged",
    "PowerState", "NetRx",      "NetUnbond",   "StoRx",   "Stop",
};
static_assert(std::size(kTags) == std::variant_size_v<Event>,
              "a new Event alternative needs its name here, in the same place");

}  // namespace

void init() noexcept {
    port::Lock lk{mx()};
    auto& m = detail::mem();
    g_warm = valid(m);
    if (g_warm) {
        g_head.store(m.head);
    } else {
        g_head.store(0);
        reset(m);
    }
    const uint32_t kept = m.head - m.base;
    g_carried = kept < kEntries ? kept : kEntries;
    m.boot_at = m.head;
    g_recorded.store(0);
}

uint8_t source(const char* name) noexcept {
    if (!name) return 0xFF;
    port::Lock lk{mx()};
    auto& m = detail::mem();
    if (!valid(m)) reset(m);  // record() before init(): still label it
    for (std::size_t i = 0; i < kSources; ++i) {
        if (m.names[i][0] == '\0') {
            std::strncpy(m.names[i], name, kNameMax - 1);
            m.names[i][kNameMax - 1] = '\0';
            return static_cast<uint8_t>(i);
        }
        if (std::strncmp(m.names[i], name, kNameMax - 1) == 0) return static_cast<uint8_t>(i);
    }
    return 0xFF;
}

void record(uint8_t src, Event const& e) noexcept {
    const uint32_t data = payload(e, std::make_index_sequence<std::variant_size_v<Event>>{});
    const auto tag = static_cast<uint8_t>(e.index());
    if (src < kSources) {
        // Each source is one AO's own thread, so its slot here has a single writer.
        const uint64_t sig = (uint64_t{1} << 40) | (uint64_t{tag} << 32) | data;
        if (g_last[src] == sig) {
            g_repeats.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_last[src] = sig;
    }
    auto& m = detail::mem();
    const uint32_t seq = g_head.fetch_add(1, std::memory_order_relaxed);
    auto& s = m.slot[seq % kEntries];
    // Neither the slot's old sequence nor this one: a reader that looks now skips it.
    store(s.seq16, static_cast<uint16_t>((seq ^ 0x8000u) & 0xFFFFu));
    std::atomic_thread_fence(std::memory_order_release);
    store(s.ms, port::now_ms());
    store(s.data, data);
    store(s.src, src);
    store(s.tag, tag);
    store(s.seq16, static_cast<uint16_t>(seq & 0xFFFFu), std::memory_order_release);
    store(m.head, seq + 1);  // a racing writer may store a smaller one; the next record fixes it
    g_recorded.fetch_add(1, std::memory_order_relaxed);
}

const char* tag_name(uint8_t tag) noexcept { return tag < std::size(kTags) ? kTags[tag] : "?"; }

std::size_t snapshot(Entry* out, std::size_t cap) noexcept {
    auto& m = detail::mem();
    const uint32_t head = g_head.load();
    uint32_t first = head > kEntries ? head - kEntries : 0;
    if (static_cast<int32_t>(m.base - first) > 0) first = m.base;
    const uint32_t boot_at = m.boot_at;
    std::size_t n = 0;
    for (uint32_t seq = first; seq != head && n < cap; ++seq) {
        auto& s = m.slot[seq % kEntries];
        const auto want = static_cast<uint16_t>(seq & 0xFFFFu);
        if (load(s.seq16, std::memory_order_acquire) != want) continue;
        Entry e{};
        e.seq = seq;
        e.ms = load(s.ms);
        const uint32_t d = load(s.data);
        std::memcpy(e.data, &d, sizeof e.data);
        e.src = load(s.src);
        e.tag = load(s.tag);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (load(s.seq16) != want) continue;  // overwritten while we copied
        e.prev = static_cast<int32_t>(seq - boot_at) < 0;
        out[n++] = e;
    }
    return n;
}

const char* source_name(uint8_t src) noexcept {
    if (src >= kSources) return "?";
    port::Lock lk{mx()};
    const char* n = detail::mem().names[src];
    return n[0] ? n : "?";
}

Stats stats() noexcept { return {g_recorded.load(), g_repeats.load(), g_carried, g_warm}; }

void clear() noexcept {
    port::Lock lk{mx()};
    auto& m = detail::mem();
    // Moving the floor rather than wiping slots: a writer mid-record is left alone, and
    // snapshot() simply starts after it.
    m.base = g_head.load();
    if (static_cast<int32_t>(m.base - m.boot_at) > 0) m.boot_at = m.base;
    g_carried = 0;
}

}  // namespace clk::evtrace
