// The fake Wi-Fi: hal::wifi's contract, with a list of access points and a scripted internet
// where the radio and the socket would be.                       [FIRMWARE.md D14, §6.7]
//
// Modelled: what `net` branches on -- an association that takes a moment and then succeeds,
// or fails for a reason a person could act on (no such network, wrong password); an SNTP
// exchange that answers, times out, cannot resolve, or answers with something no client
// should believe.  The answers are real NTP packets, built here from a settable "true UTC",
// so the parser that ships is the parser under test.  Everything runs on SIM time.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/log.hpp"

namespace clk::hal {
namespace {

constexpr uint64_t kAssocUs = 300'000;  // association + DHCP, sim time
constexpr uint64_t kRttUs = 20'000;     // an NTP round trip
// The wire facts, spelled out again rather than included: hal/ sits BELOW domain/ (§2), and
// the parser in domain/sntp.hpp is what these packets exist to test.
constexpr std::size_t kNtpPacket = 48;
constexpr int64_t kNtpToUnix = 2'208'988'800LL;

struct ApEntry {
    std::string ssid, psk;
    int8_t rssi;
};

struct Fake {
    bool started = false;
    wifi::Phase phase = wifi::Phase::Off;
    wifi::Err err = wifi::Err::None;
    uint8_t reason = 0;
    std::string ssid, psk;
    int8_t rssi = 0;
    uint64_t assoc_at_us = 0;
    // the one exchange
    bool ntp_busy = false, ntp_done = false;
    wifi::Exchange ex{};
    uint64_t ntp_due_us = 0;
    uint8_t req[kNtpPacket]{};
    std::string ntp_host;
};

std::mutex g_mx;
Fake g;
std::vector<ApEntry> g_aps;
std::map<std::string, host::NtpMode> g_ntp;  // "*" = every host not named
std::map<std::string, uint32_t> g_ntp_count;
bool g_utc_set = false;
int64_t g_utc_base_ms = 0;
uint64_t g_utc_base_us = 0;

int64_t utc_now_ms_locked() {
    const uint64_t now = clock_::micros();
    if (!g_utc_set) {
        // Default: the laptop's clock, pinned the first time anyone asks, then run on sim time.
        g_utc_base_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        g_utc_base_us = now;
        g_utc_set = true;
    }
    return g_utc_base_ms + static_cast<int64_t>((now - g_utc_base_us) / 1000);
}

host::NtpMode mode_for(std::string const& h) {
    if (auto it = g_ntp.find(h); it != g_ntp.end()) return it->second;
    if (auto it = g_ntp.find("*"); it != g_ntp.end()) return it->second;
    return host::NtpMode::Answer;
}

void put_ts(uint8_t* p, int64_t unix_ms) {
    const uint64_t s = static_cast<uint64_t>(unix_ms / 1000 + kNtpToUnix);
    const uint64_t frac = (static_cast<uint64_t>(unix_ms % 1000) << 32) / 1000;
    const uint64_t ts = (s << 32) | frac;
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(ts >> (56 - 8 * i));
}

// Settle whatever is due by now: the association, the exchange.
void advance_locked() {
    const uint64_t now = clock_::micros();
    if (g.phase == wifi::Phase::Connecting && now - g.assoc_at_us >= kAssocUs) {
        const auto it = std::find_if(g_aps.begin(), g_aps.end(),
                                     [](ApEntry const& a) { return a.ssid == g.ssid; });
        if (it == g_aps.end()) {
            g.phase = wifi::Phase::Failed;
            g.err = wifi::Err::NoAp;
            g.reason = 201;  // WIFI_REASON_NO_AP_FOUND
        } else if (it->psk != g.psk) {
            g.phase = wifi::Phase::Failed;
            g.err = wifi::Err::Auth;
            g.reason =
                15;  // WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT -- what a wrong WPA2 key looks like
        } else {
            g.phase = wifi::Phase::Connected;
            g.err = wifi::Err::None;
            g.reason = 0;
            g.rssi = it->rssi;
        }
    }
    // An AP that vanished under a live association.
    if (g.phase == wifi::Phase::Connected &&
        std::none_of(g_aps.begin(), g_aps.end(),
                     [](ApEntry const& a) { return a.ssid == g.ssid; })) {
        g.phase = wifi::Phase::Failed;
        g.err = wifi::Err::NoAp;
        g.reason = 8;  // WIFI_REASON_ASSOC_LEAVE-ish: beacon lost
        g.rssi = 0;
    }
    if (g.ntp_busy && now >= g.ntp_due_us) {
        g.ntp_busy = false;
        g.ntp_done = true;
        g.ex.recv_us = now;
        const auto mode = mode_for(g.ntp_host);
        if (mode == host::NtpMode::NoDns) {
            g.ex.st = Status::NotPresent;
        } else if (mode == host::NtpMode::Silent || g.phase != wifi::Phase::Connected) {
            g.ex.st = Status::Failed;
        } else {
            uint8_t* p = g.ex.rx;
            std::memset(p, 0, sizeof g.ex.rx);
            const unsigned li = mode == host::NtpMode::Unsynced ? 3 : 0;
            p[0] = static_cast<uint8_t>((li << 6) | (4 << 3) | 4);
            p[1] = mode == host::NtpMode::Kiss ? 0 : 2;
            if (mode == host::NtpMode::Kiss) std::memcpy(p + 12, "RATE", 4);
            std::memcpy(p + 24, g.req + 40, 8);  // originate = our transmit (the nonce)
            const int64_t t = utc_now_ms_locked() - static_cast<int64_t>(kRttUs / 2000);
            put_ts(p + 32, t);
            put_ts(p + 40, t);
            g.ex.len = kNtpPacket;
            g.ex.st = Status::Ok;
        }
    }
}

}  // namespace

namespace wifi {

Status start() noexcept {
    std::lock_guard lk{g_mx};
    if (!g.started) CLK_LOGI(net, "wifi: fake radio up");
    g.started = true;
    if (g.phase == Phase::Off) g.phase = Phase::Idle;
    return Status::Ok;
}

Status stop() noexcept {
    std::lock_guard lk{g_mx};
    g.started = false;
    g.phase = Phase::Off;
    g.rssi = 0;
    g.ntp_busy = g.ntp_done = false;
    return Status::Ok;
}

Status connect(const char* ssid, const char* psk) noexcept {
    if (!ssid || !*ssid || std::strlen(ssid) > kSsidMax || (psk && std::strlen(psk) > kPskMax))
        return Status::BadArg;
    std::lock_guard lk{g_mx};
    if (!g.started) return Status::NotReady;
    g.ssid = ssid;
    g.psk = psk ? psk : "";
    g.phase = Phase::Connecting;
    g.err = Err::None;
    g.reason = 0;
    g.rssi = 0;
    g.assoc_at_us = clock_::micros();
    return Status::Ok;
}

Status disconnect() noexcept {
    std::lock_guard lk{g_mx};
    if (!g.started) return Status::NotReady;
    g.phase = Phase::Idle;
    g.rssi = 0;
    return Status::Ok;
}

Link link() noexcept {
    std::lock_guard lk{g_mx};
    advance_locked();
    const bool on = g.phase == Phase::Connected;
    return Link{g.phase,
                g.err,
                g.reason,
                on ? g.rssi : int8_t{0},
                on ? 0x0A01A8C0u : 0u,
                static_cast<uint8_t>(on ? 6 : 0)};
}

Result<std::size_t> scan(Ap* out, std::size_t cap) noexcept {
    std::lock_guard lk{g_mx};
    if (!g.started) return Result<std::size_t>::bad(Status::NotReady);
    if (g.phase == Phase::Connecting) return Result<std::size_t>::bad(Status::Busy);
    std::vector<ApEntry> v = g_aps;
    std::sort(v.begin(), v.end(),
              [](ApEntry const& a, ApEntry const& b) { return a.rssi > b.rssi; });
    std::size_t n = 0;
    for (auto const& a : v) {
        if (n >= cap) break;
        Ap& o = out[n++];
        std::snprintf(o.ssid, sizeof o.ssid, "%s", a.ssid.c_str());
        o.rssi = a.rssi;
        o.channel = 6;
        o.open = a.psk.empty();
    }
    return Result<std::size_t>::good(n);
}

Status ntp_send(const char* host, uint16_t, const uint8_t* req, std::size_t len,
                uint32_t timeout_ms) noexcept {
    if (!host || !*host || !req || len != kNtpPacket) return Status::BadArg;
    std::lock_guard lk{g_mx};
    advance_locked();
    if (g.phase != Phase::Connected) return Status::NotReady;
    if (g.ntp_busy || g.ntp_done) return Status::Busy;
    std::memcpy(g.req, req, len);
    g.ntp_host = host;
    ++g_ntp_count[g.ntp_host];
    g.ex = Exchange{};
    g.ex.sent_us = clock_::micros();
    const auto mode = mode_for(g.ntp_host);
    g.ntp_due_us = g.ex.sent_us + (mode == host::NtpMode::Silent  ? timeout_ms * 1000ull
                                   : mode == host::NtpMode::NoDns ? 5'000
                                                                  : kRttUs);
    g.ntp_busy = true;
    return Status::Ok;
}

Status ntp_poll(Exchange& out) noexcept {
    std::lock_guard lk{g_mx};
    advance_locked();
    if (g.ntp_busy) return Status::Busy;
    if (!g.ntp_done) return Status::NotReady;
    g.ntp_done = false;
    out = g.ex;
    return Status::Ok;
}

}  // namespace wifi

namespace host {

void wifi_add_ap(const char* ssid, const char* psk, int8_t rssi) noexcept {
    std::lock_guard lk{g_mx};
    const std::string s = ssid ? ssid : "";
    std::erase_if(g_aps, [&](ApEntry const& a) { return a.ssid == s; });
    g_aps.push_back({s, psk ? psk : "", rssi});
}

void wifi_remove_ap(const char* ssid) noexcept {
    std::lock_guard lk{g_mx};
    const std::string s = ssid ? ssid : "";
    std::erase_if(g_aps, [&](ApEntry const& a) { return a.ssid == s; });
}

void wifi_clear() noexcept {
    std::lock_guard lk{g_mx};
    g_aps.clear();
    g_ntp.clear();
    g_ntp_count.clear();
    g_utc_set = false;
}

void wifi_ntp(const char* host, NtpMode m) noexcept {
    std::lock_guard lk{g_mx};
    g_ntp[host ? host : "*"] = m;
}

void wifi_set_utc(int64_t utc_ms) noexcept {
    std::lock_guard lk{g_mx};
    g_utc_base_ms = utc_ms;
    g_utc_base_us = clock_::micros();
    g_utc_set = true;
}

uint32_t wifi_ntp_requests(const char* host) noexcept {
    std::lock_guard lk{g_mx};
    const auto it = g_ntp_count.find(host ? host : "");
    return it == g_ntp_count.end() ? 0 : it->second;
}

std::string wifi_describe() {
    std::lock_guard lk{g_mx};
    std::string s;
    for (auto const& a : g_aps) {
        s += "ap \"" + a.ssid + "\" " + (a.psk.empty() ? "open" : "wpa2") + " " +
             std::to_string(a.rssi) + " dBm\n";
    }
    static constexpr const char* kMode[] = {"answer", "silent", "nodns", "kiss", "unsynced"};
    for (auto const& [h, m] : g_ntp) s += "ntp " + h + " " + kMode[static_cast<int>(m)] + "\n";
    return s;
}

}  // namespace host
}  // namespace clk::hal
