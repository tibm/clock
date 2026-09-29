// hal::wifi on esp_wifi + lwIP.                                 [FIRMWARE.md §6.7]
//
// Station mode only.  The radio, the association and one UDP exchange, no policy: `net`
// decides which network, when to retry and which time server to ask.  Credentials live in
// `net`'s NVS keys, not esp_wifi's (WIFI_STORAGE_RAM) -- one owner, and a `net wifi forget`
// that really forgets.
//
// No auto-reconnect here either: a disconnect is reported as Failed with its reason, and the
// AO's backoff decides when to try again.  A driver that retries on its own is a driver that
// keeps hammering an AP with a wrong password while the app is telling the user it gave up.
//
// The UDP exchange runs on a small worker task because DNS alone can block for seconds, and
// the thread asking is the one that also answers the phone.
#include <cstdio>
#include <cstring>
#include <mutex>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "clk/hal/hal.hpp"
#include "clk/log.hpp"

namespace clk::hal::wifi {
namespace {

std::mutex g_mx;
bool g_inited = false;
bool g_started = false;
Phase g_phase = Phase::Off;
Err g_err = Err::None;
uint8_t g_reason = 0;
uint32_t g_ip = 0;
esp_netif_t* g_netif = nullptr;

// The exchange.
struct Job {
    char host[64];
    uint16_t port;
    uint8_t req[64];
    std::size_t len;
    uint32_t timeout_ms;
};
Job g_job{};
Exchange g_ex{};
bool g_busy = false, g_done = false;
TaskHandle_t g_worker = nullptr;
SemaphoreHandle_t g_go = nullptr;

Err classify(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
#if defined(WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY)
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
#endif
            return Err::NoAp;
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
        case WIFI_REASON_802_1X_AUTH_FAILED:
        case WIFI_REASON_CONNECTION_FAIL:
            return Err::Auth;
        default:
            return Err::Other;
    }
}

void on_event(void*, esp_event_base_t base, int32_t id, void* data) {
    std::lock_guard lk{g_mx};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* d = static_cast<wifi_event_sta_disconnected_t*>(data);
        // A disconnect we asked for (disconnect(), or connect() replacing an attempt) lands
        // here too, possibly after the NEW attempt has begun -- ASSOC_LEAVE is the reason
        // the driver gives for a disconnect this station initiated.  Only one that ends an
        // attempt or a live link for any other reason is a failure.
        if (d && d->reason == WIFI_REASON_ASSOC_LEAVE && g_phase == Phase::Connecting) return;
        g_ip = 0;
        if (g_phase == Phase::Connecting || g_phase == Phase::Connected) {
            g_phase = Phase::Failed;
            g_reason = d ? d->reason : 0;
            g_err = classify(g_reason);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* e = static_cast<ip_event_got_ip_t*>(data);
        g_ip = e ? e->ip_info.ip.addr : 0;
        g_phase = Phase::Connected;
        g_err = Err::None;
        g_reason = 0;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        g_ip = 0;
        if (g_phase == Phase::Connected) {
            g_phase = Phase::Failed;
            g_err = Err::NoIp;
        }
    }
}

void worker(void*) {
    for (;;) {
        xSemaphoreTake(g_go, portMAX_DELAY);
        Job job;
        {
            std::lock_guard lk{g_mx};
            job = g_job;
        }
        Exchange ex{};
        ex.st = Status::Failed;
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        char port[8];
        std::snprintf(port, sizeof port, "%u", job.port);
        if (lwip_getaddrinfo(job.host, port, &hints, &res) != 0 || !res) {
            ex.st = Status::NotPresent;
        } else {
            const int s = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (s >= 0) {
                timeval tv{};
                tv.tv_sec = static_cast<long>(job.timeout_ms / 1000);
                tv.tv_usec = static_cast<long>((job.timeout_ms % 1000) * 1000);
                lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
                ex.sent_us = static_cast<uint64_t>(esp_timer_get_time());
                if (lwip_sendto(s, job.req, job.len, 0, res->ai_addr, res->ai_addrlen) ==
                    static_cast<int>(job.len)) {
                    const int n = lwip_recv(s, ex.rx, sizeof ex.rx, 0);
                    ex.recv_us = static_cast<uint64_t>(esp_timer_get_time());
                    if (n > 0) {
                        ex.len = static_cast<std::size_t>(n);
                        ex.st = Status::Ok;
                    }
                }
                lwip_close(s);
            }
            lwip_freeaddrinfo(res);
        }
        std::lock_guard lk{g_mx};
        g_ex = ex;
        g_busy = false;
        g_done = true;
    }
}

Status init_once() {
    if (g_inited) return Status::Ok;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return Status::Failed;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return Status::Failed;
    g_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return Status::Failed;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_event, nullptr, nullptr);
    g_go = xSemaphoreCreateBinary();
    // Core 0 with the rest of the network stack; 4 KB covers getaddrinfo.
    xTaskCreatePinnedToCore(&worker, "wifi_udp", 4096, nullptr, 5, &g_worker, 0);
    g_inited = true;
    return Status::Ok;
}

}  // namespace

Status start() noexcept {
    std::lock_guard lk{g_mx};
    if (init_once() != Status::Ok) {
        CLK_LOGE(net, "wifi: driver init failed");
        return Status::Failed;
    }
    if (g_started) return Status::Ok;
    if (esp_wifi_start() != ESP_OK) return Status::Failed;
    g_started = true;
    g_phase = Phase::Idle;
    return Status::Ok;
}

Status stop() noexcept {
    std::lock_guard lk{g_mx};
    if (!g_started) return Status::Ok;
    g_phase = Phase::Off;  // first, so the disconnect event is not read as a failure
    esp_wifi_disconnect();
    esp_wifi_stop();
    g_started = false;
    g_ip = 0;
    return Status::Ok;
}

Status connect(const char* ssid, const char* psk) noexcept {
    if (!ssid || !*ssid || std::strlen(ssid) > kSsidMax || (psk && std::strlen(psk) > kPskMax))
        return Status::BadArg;
    std::lock_guard lk{g_mx};
    if (!g_started) return Status::NotReady;
    g_phase = Phase::Idle;  // the disconnect below is ours
    esp_wifi_disconnect();
    wifi_config_t c{};
    std::memcpy(c.sta.ssid, ssid, std::strlen(ssid));
    const std::size_t pl = psk ? std::strlen(psk) : 0;
    if (pl) std::memcpy(c.sta.password, psk, pl);
    // Any WPA2-or-better network when there is a password; WPA3 (SAE) negotiated if offered.
    c.sta.threshold.authmode = pl ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    c.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    if (esp_wifi_set_config(WIFI_IF_STA, &c) != ESP_OK) return Status::BadArg;
    g_phase = Phase::Connecting;
    g_err = Err::None;
    g_reason = 0;
    if (esp_wifi_connect() != ESP_OK) {
        g_phase = Phase::Failed;
        g_err = Err::Other;
        return Status::Failed;
    }
    return Status::Ok;
}

Status disconnect() noexcept {
    std::lock_guard lk{g_mx};
    if (!g_started) return Status::NotReady;
    g_phase = Phase::Idle;
    esp_wifi_disconnect();
    g_ip = 0;
    return Status::Ok;
}

Link link() noexcept {
    std::lock_guard lk{g_mx};
    Link l{g_phase, g_err, g_reason, 0, g_ip, 0};
    if (g_phase == Phase::Connected) {
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            l.rssi = ap.rssi;
            l.channel = ap.primary;
        }
    }
    return l;
}

Result<std::size_t> scan(Ap* out, std::size_t cap) noexcept {
    {
        std::lock_guard lk{g_mx};
        if (!g_started) return Result<std::size_t>::bad(Status::NotReady);
        if (g_phase == Phase::Connecting) return Result<std::size_t>::bad(Status::Busy);
    }
    // Blocking, outside the lock: the event handler needs it while the scan runs.
    if (esp_wifi_scan_start(nullptr, true) != ESP_OK) return Result<std::size_t>::bad(Status::Busy);
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    constexpr uint16_t kMax = 24;
    static wifi_ap_record_t recs[kMax];  // 24 x ~80 B: not on somebody's stack
    uint16_t got = n < kMax ? n : kMax;
    if (esp_wifi_scan_get_ap_records(&got, recs) != ESP_OK)
        return Result<std::size_t>::bad(Status::Failed);
    std::size_t k = 0;
    for (uint16_t i = 0; i < got && k < cap; ++i) {  // IDF sorts by RSSI already
        if (recs[i].ssid[0] == 0) continue;          // hidden network
        Ap& a = out[k++];
        std::snprintf(a.ssid, sizeof a.ssid, "%s", reinterpret_cast<const char*>(recs[i].ssid));
        a.rssi = recs[i].rssi;
        a.channel = recs[i].primary;
        a.open = recs[i].authmode == WIFI_AUTH_OPEN;
    }
    return Result<std::size_t>::good(k);
}

Status ntp_send(const char* host, uint16_t port, const uint8_t* req, std::size_t len,
                uint32_t timeout_ms) noexcept {
    if (!host || !*host || std::strlen(host) >= sizeof g_job.host || !req || len == 0 ||
        len > sizeof g_job.req)
        return Status::BadArg;
    std::lock_guard lk{g_mx};
    if (g_phase != Phase::Connected || !g_go) return Status::NotReady;
    if (g_busy || g_done) return Status::Busy;
    std::snprintf(g_job.host, sizeof g_job.host, "%s", host);
    g_job.port = port;
    std::memcpy(g_job.req, req, len);
    g_job.len = len;
    g_job.timeout_ms = timeout_ms;
    g_busy = true;
    xSemaphoreGive(g_go);
    return Status::Ok;
}

Status ntp_poll(Exchange& out) noexcept {
    std::lock_guard lk{g_mx};
    if (g_busy) return Status::Busy;
    if (!g_done) return Status::NotReady;
    g_done = false;
    out = g_ex;
    return Status::Ok;
}

}  // namespace clk::hal::wifi
