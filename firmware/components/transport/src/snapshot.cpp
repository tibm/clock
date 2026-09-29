#include "clk/transport/snapshot.hpp"

#include <cstring>
#include <type_traits>

namespace clk::transport {
namespace {

// Explicit little-endian, field by field.  A memcpy of the struct would be shorter and would
// silently change the wire the day somebody reorders a member or a compiler pads one.
class Writer {
public:
    explicit Writer(uint8_t* p) noexcept : p_(p) {}
    void u8(uint8_t v) noexcept { p_[n_++] = v; }
    void u16(uint16_t v) noexcept {
        u8(static_cast<uint8_t>(v));
        u8(static_cast<uint8_t>(v >> 8));
    }
    void u32(uint32_t v) noexcept {
        u16(static_cast<uint16_t>(v));
        u16(static_cast<uint16_t>(v >> 16));
    }
    void u64(uint64_t v) noexcept {
        u32(static_cast<uint32_t>(v));
        u32(static_cast<uint32_t>(v >> 32));
    }
    void f32(float f) noexcept {
        uint32_t v;
        std::memcpy(&v, &f, sizeof v);
        u32(v);
    }
    [[nodiscard]] std::size_t size() const noexcept { return n_; }

private:
    uint8_t* p_;
    std::size_t n_ = 0;
};

class Reader {
public:
    explicit Reader(const uint8_t* p) noexcept : p_(p) {}
    uint8_t u8() noexcept { return p_[n_++]; }
    uint16_t u16() noexcept {
        const uint16_t lo = u8();
        return static_cast<uint16_t>(lo | (u8() << 8));
    }
    uint32_t u32() noexcept {
        const uint32_t lo = u16();
        return lo | (static_cast<uint32_t>(u16()) << 16);
    }
    uint64_t u64() noexcept {
        const uint64_t lo = u32();
        return lo | (static_cast<uint64_t>(u32()) << 32);
    }
    float f32() noexcept {
        const uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, sizeof f);
        return f;
    }
    [[nodiscard]] std::size_t size() const noexcept { return n_; }

private:
    const uint8_t* p_;
    std::size_t n_ = 0;
};

// One list, walked by both directions, so encode and decode cannot disagree about order.
template <class Io, class S>
void fields(Io& io, S& s) noexcept {
    constexpr bool kW = std::is_same_v<Io, Writer>;
    auto u8 = [&](auto& v) {
        if constexpr (kW)
            io.u8(static_cast<uint8_t>(v));
        else
            v = static_cast<std::remove_reference_t<decltype(v)>>(io.u8());
    };
    auto u16 = [&](auto& v) {
        if constexpr (kW)
            io.u16(static_cast<uint16_t>(v));
        else
            v = static_cast<std::remove_reference_t<decltype(v)>>(io.u16());
    };
    auto u32 = [&](auto& v) {
        if constexpr (kW)
            io.u32(static_cast<uint32_t>(v));
        else
            v = static_cast<std::remove_reference_t<decltype(v)>>(io.u32());
    };
    auto u64 = [&](auto& v) {
        if constexpr (kW)
            io.u64(static_cast<uint64_t>(v));
        else
            v = static_cast<std::remove_reference_t<decltype(v)>>(io.u64());
    };
    auto f32 = [&](auto& v) {
        if constexpr (kW)
            io.f32(v);
        else
            v = io.f32();
    };

    u16(s.seq);                         //   2
    u32(s.uptime_s);                    //   4
    u64(s.epoch_ms);                    //   8
    u16(s.tz_off_min);                  //  16
    u8(s.reset_reason);                 //  18
    u8(s.clk_src);                      //  19
    u32(s.flags);                       //  20
    u32(s.fw_id);                       //  24
    u32(s.heap_free);                   //  28
    u32(s.heap_min);                    //  32
    u16(s.vbat_mv);                     //  36
    u8(s.soc_pct);                      //  38
    u8(s.vbat_src);                     //  39
    u16(s.temp_cdeg);                   //  40
    u16(s.rh_cpct);                     //  42
    u16(s.press_dhpa);                  //  44
    u32(s.gas_ohms);                    //  46
    u16(s.env_age_s);                   //  50
    f32(s.lux);                         //  52
    u16(s.als_age_s);                   //  56
    for (auto& g : s.grav_mm) u16(g);   //  58 60 62
    for (auto& a : s.ypr_cdeg) u16(a);  //  64 66 68
    u16(s.taps);                        //  70
    u8(s.motion_state);                 //  72
    u8(s.dial_tick);                    //  73
    u8(s.hand_h);                       //  74
    u8(s.hand_m);                       //  75
    u8(s.target_h);                     //  76
    u8(s.target_m);                     //  77
    u16(s.opto);                        //  78
    u32(s.motion_faults);               //  80
    u16(s.trims);                       //  84
    u16(s.last_trim);                   //  86
    u8(s.ui_mode);                      //  88
    u8(s.volume);                       //  89
    u8(s.alarm_h);                      //  90
    u8(s.alarm_m);                      //  91
    u8(s.brightness);                   //  92
    u8(s.wake_warm);                    //  93
    u8(s.wake_cool);                    //  94
    u8(s.ble_state);                    //  95
    for (auto& p : s.px)                //  96 .. 123
        for (auto& c : p) u8(c);
    u32(s.knob_count);  // 124
    u8(s.bonds);        // 128
    u8(s.wifi_state);   // 129
    u8(s.wifi_rssi);    // 130
    u8(s.wifi_err);     // 131 (was reserved, always 0, until 2026-09-28)
}

}  // namespace

const char* name(WifiState s) noexcept {
    switch (s) {
        case WifiState::Off:
            return "off";
        case WifiState::Idle:
            return "idle";
        case WifiState::Connecting:
            return "connecting";
        case WifiState::Online:
            return "online";
        case WifiState::Backoff:
            return "backoff";
    }
    return "?";
}

const char* name(WifiErr e) noexcept {
    switch (e) {
        case WifiErr::None:
            return "none";
        case WifiErr::NoAp:
            return "no-ap";
        case WifiErr::Auth:
            return "auth";
        case WifiErr::NoIp:
            return "no-ip";
        case WifiErr::Timeout:
            return "timeout";
        case WifiErr::Other:
            return "other";
    }
    return "?";
}

const char* name(BleState s) noexcept {
    switch (s) {
        case BleState::Off:
            return "off";
        case BleState::Idle:
            return "idle";
        case BleState::Pairing:
            return "pairing";
        case BleState::Connected:
            return "connected";
        case BleState::Secure:
            return "secure";
    }
    return "?";
}

uint32_t fw_id(const char* d) noexcept {
    if (!d) return 0;
    auto hex = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    // The sha is the LAST run of 7+ hex digits: after the `-g` of a tag form, before `-dirty`.
    const char* best = nullptr;
    std::size_t best_n = 0;
    for (const char* p = d; *p;) {
        if (!hex(*p)) {
            ++p;
            continue;
        }
        const char* q = p;
        while (*q && hex(*q)) ++q;
        const auto n = static_cast<std::size_t>(q - p);
        if (n >= 7) {
            best = p;
            best_n = n;
        }
        p = q;
    }
    if (!best) return 0;
    uint32_t v = 0;
    for (std::size_t i = 0; i < best_n && i < 8; ++i) {
        const char c = best[i];
        v = v * 16 + static_cast<uint32_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return v;
}

std::size_t encode(Snapshot const& s, uint8_t* out, std::size_t cap) noexcept {
    if (!out || cap < kWireSize) return 0;
    Writer w{out};
    w.u8(kSchema);
    w.u8(static_cast<uint8_t>(kWireSize));
    Snapshot copy = s;  // fields() is one template for both ways; it wants a mutable ref
    fields(w, copy);
    return w.size() == kWireSize ? kWireSize : 0;
}

bool decode(const uint8_t* in, std::size_t len, Snapshot& out) noexcept {
    if (!in || len < kWireSize) return false;
    if (in[0] != kSchema || in[1] < kWireSize) return false;
    Reader r{in + 2};
    out = Snapshot{};
    fields(r, out);
    return r.size() + 2 == kWireSize;
}

}  // namespace clk::transport
