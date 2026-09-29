// `net`, the Wi-Fi half: one stored network, kept associated, and the time from it.
// [FIRMWARE.md §6.7, app/PROTOCOL.md "Wi-Fi"]
//
// The network arrives from the phone over the bonded, encrypted BLE link as an ordinary CLI
// line (`net wifi join`), so it never crosses anything a stranger could read.  It is kept in
// NVS hex-encoded -- a password may hold any byte -- and never printed or logged.
//
// Association: attempt, give it 20 s, and on failure back off 5 s -> 5 min.  The reason is
// kept (no such network / wrong password / no address) because it is the one thing the app
// can tell a person to fix.  The driver never retries on its own (hal::wifi).
//
// Time: SNTP to three free public servers, ALWAYS in the same order, the next one only when
// the one before gives no believable answer (timeout, no DNS, kiss-of-death, unsynchronised,
// wrong origin, a date before this firmware).  A good answer goes to `chrono` as UTC; the
// zone is `chrono`'s.  Then again every hour -- the crystal drifts ~0.07 s in that time --
// and after a round with no answer at all, again in 30 s, backing off to 15 min.
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "clk/domain/sntp.hpp"
#include "clk/log.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/net.hpp"

namespace clk::svc {
namespace {

using transport::WifiErr;
using transport::WifiState;

constexpr const char* kKeySsid = "net.ssid";  // NVS, hex
constexpr const char* kKeyPsk = "net.psk";    // NVS, hex

constexpr uint64_t kConnectTimeoutUs = 20'000'000;
constexpr uint32_t kBackoffS[] = {5, 15, 30, 60, 120, 300};
constexpr uint32_t kSntpTimeoutMs = 3000;  // per server
constexpr uint64_t kResyncUs = 3'600'000'000ull;
constexpr uint32_t kSntpRetryS[] = {30, 60, 120, 300, 900};

template <std::size_t N>
uint32_t step(const uint32_t (&t)[N], uint8_t i) {
    return t[i < N ? i : N - 1];
}

void to_hex(const char* s, char* out, std::size_t cap) {
    static constexpr char k[] = "0123456789abcdef";
    std::size_t n = 0;
    for (; *s && n + 2 < cap; ++s) {
        const auto b = static_cast<uint8_t>(*s);
        out[n++] = k[b >> 4];
        out[n++] = k[b & 15];
    }
    out[n] = '\0';
}

int nib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool from_hex(const char* h, char* out, std::size_t cap) {
    std::size_t n = 0;
    for (; h[0] && h[1]; h += 2) {
        const int a = nib(h[0]), b = nib(h[1]);
        if (a < 0 || b < 0 || n + 1 >= cap) return false;
        const char c = static_cast<char>((a << 4) | b);
        if (c == '\0') return false;
        out[n++] = c;
    }
    out[n] = '\0';
    return *h == '\0';
}

WifiErr map_err(hal::wifi::Err e) {
    switch (e) {
        case hal::wifi::Err::None:
            return WifiErr::None;
        case hal::wifi::Err::NoAp:
            return WifiErr::NoAp;
        case hal::wifi::Err::Auth:
            return WifiErr::Auth;
        case hal::wifi::Err::NoIp:
            return WifiErr::NoIp;
        default:
            return WifiErr::Other;
    }
}

// A nonce for the SNTP origin check.  Not a secret -- it only has to differ between requests
// and be unguessable enough that a stale datagram does not match.
uint64_t mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

}  // namespace

// ---- any thread --------------------------------------------------------------------------

Status Net::wifi_join(const char* ssid, const char* psk) noexcept {
    const std::size_t sl = ssid ? std::strlen(ssid) : 0;
    const std::size_t pl = psk ? std::strlen(psk) : 0;
    if (sl == 0 || sl > hal::wifi::kSsidMax) return Status::BadArg;
    if (pl != 0 && (pl < 8 || pl > hal::wifi::kPskMax)) return Status::BadArg;
    if (pl == hal::wifi::kPskMax) {  // 64 = a raw PSK, which is hex by definition
        for (std::size_t i = 0; i < pl; ++i)
            if (nib(psk[i]) < 0) return Status::BadArg;
    }
    char hs[2 * hal::wifi::kSsidMax + 1], hp[2 * hal::wifi::kPskMax + 1];
    to_hex(ssid, hs, sizeof hs);
    to_hex(pl ? psk : "", hp, sizeof hp);
    const Status st = hal::store::set_str(kKeySsid, hs);
    if (st == Status::Ok) (void)hal::store::set_str(kKeyPsk, hp);
    port::Lock lk{mx_};
    std::snprintf(wifi_.ssid, sizeof wifi_.ssid, "%s", ssid);
    std::snprintf(psk_, sizeof psk_, "%s", pl ? psk : "");
    wifi_.provisioned = true;
    wifi_.attempts = 0;
    wifi_.err = WifiErr::None;
    wifi_.reason = 0;
    join_pending_ = true;
    if (wifi_up_) wifi_.state = WifiState::Connecting;
    return st == Status::Failed ? Status::Failed : Status::Ok;
}

Status Net::wifi_forget() noexcept {
    (void)hal::store::set_str(kKeySsid, "");
    (void)hal::store::set_str(kKeyPsk, "");
    port::Lock lk{mx_};
    wifi_.provisioned = false;
    wifi_.ssid[0] = '\0';
    std::memset(psk_, 0, sizeof psk_);
    wifi_.err = WifiErr::None;
    wifi_.reason = 0;
    wifi_.attempts = 0;
    join_pending_ = false;
    forget_pending_ = true;
    if (wifi_up_) wifi_.state = WifiState::Idle;
    return Status::Ok;
}

void Net::sntp_now() noexcept {
    port::Lock lk{mx_};
    sntp_pending_ = true;
}

Net::Wifi Net::wifi() const noexcept {
    const uint64_t now = port::now_us();
    port::Lock lk{mx_};
    Wifi w = wifi_;
    w.retry_in_ms = w.state == WifiState::Backoff && retry_at_us_ > now
                        ? static_cast<uint32_t>((retry_at_us_ - now) / 1000)
                        : 0;
    w.synced_ago_s = w.synced ? static_cast<uint32_t>((now - sync_at_us_) / 1'000'000) : 0;
    w.next_sync_in_ms = w.state == WifiState::Online && !sntp_busy_ && next_sync_us_ > now
                            ? static_cast<uint32_t>((next_sync_us_ - now) / 1000)
                            : 0;
    return w;
}

// ---- this AO's thread --------------------------------------------------------------------

void Net::wifi_boot() noexcept {
    char hs[2 * hal::wifi::kSsidMax + 1] = "", hp[2 * hal::wifi::kPskMax + 1] = "";
    char ssid[hal::wifi::kSsidMax + 1] = "", psk[hal::wifi::kPskMax + 1] = "";
    const bool have = hal::store::get_str(kKeySsid, hs, sizeof hs) == Status::Ok && hs[0] &&
                      from_hex(hs, ssid, sizeof ssid) &&
                      (hal::store::get_str(kKeyPsk, hp, sizeof hp) != Status::Ok ||
                       from_hex(hp, psk, sizeof psk));
    {
        port::Lock lk{mx_};
        wifi_.server = wifi_.trying = -1;
        if (have) {
            std::snprintf(wifi_.ssid, sizeof wifi_.ssid, "%s", ssid);
            std::snprintf(psk_, sizeof psk_, "%s", psk);
            wifi_.provisioned = true;
        }
    }
    std::memset(psk, 0, sizeof psk);
    if (have) CLK_LOGI(net, "wifi: network \"%s\" stored", ssid);
    report_net();
}

void Net::wifi_radio(bool on) noexcept {
    if (!on) {
        bool was;
        {
            port::Lock lk{mx_};
            was = wifi_up_;
            wifi_up_ = false;
            wifi_.state = WifiState::Off;
            wifi_.link = {};
        }
        if (was) {
            (void)hal::wifi::stop();
            CLK_LOGI(net, "RADIO_OFF: Wi-Fi stopped");
        }
        sntp_busy_ = false;
        return;
    }
    const Status st = hal::wifi::start();
    bool prov;
    {
        port::Lock lk{mx_};
        wifi_up_ = st == Status::Ok;
        prov = wifi_.provisioned;
        wifi_.state = !wifi_up_ ? WifiState::Off : prov ? WifiState::Connecting : WifiState::Idle;
    }
    if (st != Status::Ok) {
        if (st != Status::NotPresent) CLK_LOGW(net, "Wi-Fi did not start: %s", clk::name(st));
        return;
    }
    backoff_ = 0;
    if (prov) wifi_connect(port::now_us());
}

void Net::wifi_connect(uint64_t now) noexcept {
    char ssid[hal::wifi::kSsidMax + 1], psk[hal::wifi::kPskMax + 1];
    uint32_t n;
    {
        port::Lock lk{mx_};
        if (!wifi_up_) return;
        if (!wifi_.provisioned) {
            wifi_.state = WifiState::Idle;
            return;
        }
        std::memcpy(ssid, wifi_.ssid, sizeof ssid);
        std::memcpy(psk, psk_, sizeof psk);
        n = ++wifi_.attempts;
        wifi_.state = WifiState::Connecting;
    }
    attempt_at_us_ = now;
    sntp_busy_ = false;
    const Status st = hal::wifi::connect(ssid, psk);
    std::memset(psk, 0, sizeof psk);
    if (st != Status::Ok) {
        wifi_fail(now, WifiErr::Other, 0);
        return;
    }
    CLK_LOGI(net, "wifi: joining \"%s\" (attempt %" PRIu32 ")", ssid, n);
}

void Net::wifi_fail(uint64_t now, WifiErr err, uint8_t reason) noexcept {
    const uint32_t s = step(kBackoffS, backoff_);
    if (backoff_ < 255) ++backoff_;
    retry_at_us_ = now + s * 1'000'000ull;
    sntp_busy_ = false;
    char ssid[hal::wifi::kSsidMax + 1];
    {
        port::Lock lk{mx_};
        wifi_.state = WifiState::Backoff;
        wifi_.err = err;
        wifi_.reason = reason;
        wifi_.trying = -1;
        std::memcpy(ssid, wifi_.ssid, sizeof ssid);
    }
    CLK_LOGW(net, "wifi: \"%s\" failed: %s (reason %u); retry in %" PRIu32 " s", ssid,
             transport::name(err), reason, s);
}

void Net::wifi_tick(uint64_t now) noexcept {
    bool join, forget, up;
    WifiState state;
    {
        port::Lock lk{mx_};
        join = join_pending_;
        forget = forget_pending_;
        join_pending_ = forget_pending_ = false;
        up = wifi_up_;
        state = wifi_.state;
    }
    if (!up) return;
    if (forget) {
        (void)hal::wifi::disconnect();
        sntp_busy_ = false;
        CLK_LOGI(net, "wifi: network forgotten");
        report_net();
    }
    if (join) {
        backoff_ = 0;
        wifi_connect(now);
        report_net();
    }
    const auto l = hal::wifi::link();
    {
        port::Lock lk{mx_};
        wifi_.link = l;
        state = wifi_.state;
    }
    switch (state) {
        case WifiState::Connecting:
            if (l.phase == hal::wifi::Phase::Connected) {
                backoff_ = 0;
                char ssid[hal::wifi::kSsidMax + 1];
                {
                    port::Lock lk{mx_};
                    wifi_.state = WifiState::Online;
                    wifi_.err = WifiErr::None;
                    wifi_.reason = 0;
                    std::memcpy(ssid, wifi_.ssid, sizeof ssid);
                }
                CLK_LOGI(net, "wifi: online on \"%s\", %d dBm, ch %u", ssid, l.rssi, l.channel);
            } else if (l.phase == hal::wifi::Phase::Failed) {
                wifi_fail(now, map_err(l.err), l.reason);
            } else if (now > attempt_at_us_ && now - attempt_at_us_ >= kConnectTimeoutUs) {
                (void)hal::wifi::disconnect();
                wifi_fail(now, WifiErr::Timeout, 0);
            }
            break;
        case WifiState::Online:
            if (l.phase != hal::wifi::Phase::Connected) {
                CLK_LOGW(net, "wifi: link lost");
                backoff_ = 0;
                wifi_fail(now,
                          l.phase == hal::wifi::Phase::Failed ? map_err(l.err) : WifiErr::Other,
                          l.reason);
            } else {
                sntp_tick(now);
            }
            break;
        case WifiState::Backoff:
            if (now >= retry_at_us_) wifi_connect(now);
            break;
        default:
            break;
    }
}

// ---- SNTP --------------------------------------------------------------------------------

void Net::sntp_send(uint64_t now) noexcept {
    // A previous exchange that finished after its link went away still holds the slot.
    hal::wifi::Exchange stale;
    if (hal::wifi::ntp_poll(stale) == Status::Busy) return;  // still running: next tick
    const char* host = kSntpServers[sntp_idx_];
    sntp_nonce_ = mix(now ^ (static_cast<uint64_t>(sntp_idx_) << 56) ^ sntp_nonce_);
    uint8_t req[domain::sntp::kPacket];
    domain::sntp::build_request(req, sntp_nonce_);
    const Status st =
        hal::wifi::ntp_send(host, domain::sntp::kPort, req, sizeof req, kSntpTimeoutMs);
    if (st != Status::Ok) return;  // not connected (the link check catches it) or busy
    sntp_busy_ = true;
    port::Lock lk{mx_};
    wifi_.trying = static_cast<int8_t>(sntp_idx_);
}

void Net::sntp_round_failed(uint64_t now) noexcept {
    const uint32_t s = step(kSntpRetryS, sntp_backoff_);
    if (sntp_backoff_ < 255) ++sntp_backoff_;
    next_sync_us_ = now + s * 1'000'000ull;
    sntp_idx_ = 0;
    {
        port::Lock lk{mx_};
        ++wifi_.fails;
        wifi_.trying = -1;
    }
    CLK_LOGW(net, "sntp: no server gave the time; next round in %" PRIu32 " s", s);
}

void Net::sntp_tick(uint64_t now) noexcept {
    if (!sntp_busy_) {
        bool asked;
        {
            port::Lock lk{mx_};
            asked = sntp_pending_;
            sntp_pending_ = false;
        }
        if (asked || now >= next_sync_us_) {
            sntp_idx_ = 0;
            sntp_send(now);
        }
        return;
    }
    hal::wifi::Exchange ex{};
    const Status pst = hal::wifi::ntp_poll(ex);
    if (pst == Status::Busy) return;
    sntp_busy_ = false;
    const char* host = kSntpServers[sntp_idx_];
    const char* why = nullptr;
    domain::sntp::Reply r{};
    if (pst != Status::Ok) {
        why = "lost";
    } else if (ex.st == Status::NotPresent) {
        why = "no DNS answer";
    } else if (ex.st != Status::Ok) {
        why = "timeout";
    } else if (const auto v = domain::sntp::parse(ex.rx, ex.len, sntp_nonce_, r);
               v != domain::sntp::Verdict::Ok) {
        why = domain::sntp::name(v);
    }
    if (why) {
        CLK_LOGW(net, "sntp: %s: %s%s", host, why, sntp_idx_ + 1 < kSntpCount ? " -- next" : "");
        {
            port::Lock lk{mx_};
            std::snprintf(wifi_.last_fail, sizeof wifi_.last_fail, "%s: %s", host, why);
            wifi_.trying = -1;
        }
        if (++sntp_idx_ < kSntpCount) {
            sntp_send(now);
        } else {
            sntp_round_failed(now);
        }
        return;
    }

    // UTC now = UTC when the answer landed + how long it has sat since.
    const uint64_t mono = hal::clock_::micros();
    const int64_t utc = domain::sntp::utc_at_arrival_ms(r, ex.sent_us, ex.recv_us) +
                        static_cast<int64_t>((mono - ex.recv_us) / 1000);
    int64_t step_ms = 0;
    bool first;
    {
        port::Lock lk{mx_};
        first = !wifi_.synced;
    }
    if (chrono_) {
        const auto c = chrono_->snapshot();
        if (c.valid && c.date_valid) step_ms = utc - chrono_->now_epoch_ms();
        chrono_->set_utc(utc, Chrono::Source::Sntp);
    }
    sync_at_us_ = now;
    next_sync_us_ = now + kResyncUs;
    sntp_backoff_ = 0;
    {
        port::Lock lk{mx_};
        wifi_.synced = true;
        wifi_.server = static_cast<int8_t>(sntp_idx_);
        wifi_.trying = -1;
        wifi_.last_step_ms = step_ms;
        ++wifi_.syncs;
    }
    CLK_LOGI(net, "sntp: %s answered (stratum %u, rtt %" PRIu64 " ms)%s", host, r.stratum,
             (ex.recv_us - ex.sent_us) / 1000, first ? " -- the clock is set" : "");
    sntp_idx_ = 0;
    report_net();
}

// `chrono` holds the two facts `ui` reads to lock the knob out of `clock` mode.  Only on a
// change, so `chrono net` on the bench is not overwritten every tick.
void Net::report_net() noexcept {
    bool prov, sync;
    {
        port::Lock lk{mx_};
        prov = wifi_.provisioned;
        sync = wifi_.synced;
    }
    if (reported_ && prov == reported_prov_ && sync == reported_sync_) return;
    reported_ = true;
    reported_prov_ = prov;
    reported_sync_ = sync;
    if (chrono_) chrono_->set_net(prov, sync);
}

}  // namespace clk::svc
