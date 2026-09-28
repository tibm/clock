// hal::ble on NimBLE.                                          [FIRMWARE.md §8.2, §6.7]
//
// One vendor service, four characteristics, one connection.  What lives here is the radio,
// the GATT table and the ONE security rule nothing above may forget: the service answers an
// encrypted, bonded link and nothing else.  The pairing window, the command channel and the
// status cadence are the `net` AO's (services/src/net.cpp).
//
// How the window is enforced, since NimBLE has no "refuse this pairing request" hook:
//   - closed: `sm_bonding = 0`.  A stranger can still run Just Works and get an encrypted
//     link, but no bond is stored, and the ENC_CHANGE handler drops any encrypted link that
//     is not bonded.  Every characteristic also refuses it in the access callback, so there is
//     no instant between the two in which a command could get through.
//   - open: `sm_bonding = 1`.  The next phone to pair is stored.
//   A bonded phone reconnecting re-encrypts from its stored LTK, which is `bonded` whether or
//   not the window is open.
//
// RADIO_OFF (§6.7) is stop(): the link is dropped and advertising stops, so nothing
// transmits.  The controller stays initialised -- a quiet stack, not a torn-down one, because
// NimBLE's deinit/re-init path is the least-exercised code in it and the toggle is a switch
// people flick.  Nothing is emitted either way.
#include <cstdio>
#include <cstring>
#include <mutex>

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "clk/hal/hal.hpp"
#include "clk/log.hpp"

extern "C" void ble_store_config_init(void);  // NimBLE ships no header for it

namespace clk::hal::ble {
namespace {

// 7a3e000X-5c1d-4b8e-9f3a-2c6d1e0b9a41, least-significant byte first as NimBLE wants it.
#define CLK_UUID(x)                                                                               \
    BLE_UUID128_INIT(0x41, 0x9a, 0x0b, 0x1e, 0x6d, 0x2c, 0x3a, 0x9f, 0x8e, 0x4b, 0x1d, 0x5c, (x), \
                     0x00, 0x3e, 0x7a)
const ble_uuid128_t kSvcUuid = CLK_UUID(0x01);
const ble_uuid128_t kCmdUuid = CLK_UUID(0x02);
const ble_uuid128_t kRspUuid = CLK_UUID(0x03);
const ble_uuid128_t kStatusUuid = CLK_UUID(0x04);
const ble_uuid128_t kInfoUuid = CLK_UUID(0x05);

// Advertising interval, 0.625 ms units.  Fast while somebody is trying to find the clock;
// slow the rest of the time, when the only listener is a phone that already knows it.
constexpr uint16_t kAdvFastItvl = 160;   // 100 ms
constexpr uint16_t kAdvSlowItvl = 1600;  // 1 s
// Manufacturer data: 0xFFFF is the SIG's "no company, testing" id.  One state byte after it,
// bit 0 = pairing window open, so an app can list "ready to pair" without connecting.
constexpr uint16_t kCompanyId = 0xFFFF;

constexpr std::size_t kStatusCap = 244;  // one notification at the 247 MTU we ask for
constexpr std::size_t kInfoCap = 200;

std::mutex g_mx;
bool g_inited = false;  // nimble_port_init() done -- once per boot
bool g_synced = false;  // the host and controller agree; advertising is possible
bool g_up = false;      // start() called and stop() not since
bool g_pairable = false;
bool g_advertising = false;
uint16_t g_conn = BLE_HS_CONN_HANDLE_NONE;
bool g_encrypted = false, g_bonded = false;
bool g_rsp_sub = false, g_status_sub = false;
uint16_t g_mtu = 23;
uint8_t g_bonds = 0;
uint32_t g_paired = 0, g_refused = 0;
uint8_t g_own_addr_type = 0;
char g_name[32] = "clock";
RxFn g_rx = nullptr;

uint8_t g_status[kStatusCap];
std::size_t g_status_len = 0;
char g_info[kInfoCap] = "";

uint16_t g_rsp_handle = 0, g_status_handle = 0;

int gap_event(ble_gap_event* ev, void*);

uint8_t count_bonds() {
    int n = 0;
    if (::ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &n) != 0) n = 0;
    return static_cast<uint8_t>(n < 0 ? 0 : (n > 255 ? 255 : n));
}

// The access rule, in one place: encrypted AND bonded.  NimBLE's _ENC flags already force
// encryption; this adds the bond, which is what closes the window for a stranger who ran Just
// Works while bonding was off.
bool link_trusted(uint16_t conn) {
    ble_gap_conn_desc d{};
    if (::ble_gap_conn_find(conn, &d) != 0) return false;
    return d.sec_state.encrypted && d.sec_state.bonded;
}

int access(uint16_t conn, uint16_t attr, ble_gatt_access_ctxt* ctxt, void*) {
    (void)attr;
    if (!link_trusted(conn)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    const ble_uuid_t* u = ctxt->chr->uuid;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && ::ble_uuid_cmp(u, &kCmdUuid.u) == 0) {
        uint8_t buf[kMaxWrite];
        uint16_t n = 0;
        const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len == 0 || len > kMaxWrite) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (::ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf, &n) != 0) return BLE_ATT_ERR_UNLIKELY;
        RxFn rx;
        {
            std::lock_guard lk{g_mx};
            rx = g_rx;
        }
        if (rx) rx(buf, n);  // copies and returns: we are on the host task
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        std::lock_guard lk{g_mx};
        if (::ble_uuid_cmp(u, &kStatusUuid.u) == 0) {
            return ::os_mbuf_append(ctxt->om, g_status, g_status_len) == 0
                       ? 0
                       : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (::ble_uuid_cmp(u, &kInfoUuid.u) == 0) {
            return ::os_mbuf_append(ctxt->om, g_info, std::strlen(g_info)) == 0
                       ? 0
                       : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (::ble_uuid_cmp(u, &kRspUuid.u) == 0) return 0;  // notify-only in spirit; empty read
    }
    return BLE_ATT_ERR_UNLIKELY;
}

const ble_gatt_chr_def kChrs[] = {
    {.uuid = &kCmdUuid.u,
     .access_cb = access,
     .arg = nullptr,
     .descriptors = nullptr,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
     .min_key_size = 16,
     .val_handle = nullptr,
     .cpfd = nullptr},
    {.uuid = &kRspUuid.u,
     .access_cb = access,
     .arg = nullptr,
     .descriptors = nullptr,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
     .min_key_size = 16,
     .val_handle = &g_rsp_handle,
     .cpfd = nullptr},
    {.uuid = &kStatusUuid.u,
     .access_cb = access,
     .arg = nullptr,
     .descriptors = nullptr,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
     .min_key_size = 16,
     .val_handle = &g_status_handle,
     .cpfd = nullptr},
    {.uuid = &kInfoUuid.u,
     .access_cb = access,
     .arg = nullptr,
     .descriptors = nullptr,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
     .min_key_size = 16,
     .val_handle = nullptr,
     .cpfd = nullptr},
    {},
};

const ble_gatt_svc_def kSvcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &kSvcUuid.u,
     .includes = nullptr,
     .characteristics = kChrs},
    {},
};

// Call with g_mx held.  Restarts advertising with the current window state -- the interval
// and the manufacturer byte both depend on it.
void advertise_locked() {
    if (!g_synced || !g_up || g_conn != BLE_HS_CONN_HANDLE_NONE) return;
    if (g_advertising) {
        (void)::ble_gap_adv_stop();
        g_advertising = false;
    }

    const uint8_t mfg[3] = {static_cast<uint8_t>(kCompanyId & 0xFF),
                            static_cast<uint8_t>(kCompanyId >> 8),
                            static_cast<uint8_t>(g_pairable ? 0x01 : 0x00)};
    ble_hs_adv_fields f{};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = &kSvcUuid;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    f.mfg_data = mfg;
    f.mfg_data_len = sizeof mfg;
    int rc = ::ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        CLK_LOGW(net, "ble: adv fields rc=%d", rc);
        return;
    }
    ble_hs_adv_fields sr{};
    sr.name = reinterpret_cast<const uint8_t*>(g_name);
    sr.name_len = static_cast<uint8_t>(std::strlen(g_name));
    sr.name_is_complete = 1;
    rc = ::ble_gap_adv_rsp_set_fields(&sr);
    if (rc != 0) {
        CLK_LOGW(net, "ble: scan-rsp fields rc=%d", rc);
        return;
    }

    ble_gap_adv_params p{};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = g_pairable ? kAdvFastItvl : kAdvSlowItvl;
    p.itvl_max = static_cast<uint16_t>(p.itvl_min + p.itvl_min / 4);
    rc = ::ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &p, gap_event, nullptr);
    if (rc != 0) {
        CLK_LOGW(net, "ble: adv start rc=%d", rc);
        return;
    }
    g_advertising = true;
}

void clear_link_locked() {
    g_conn = BLE_HS_CONN_HANDLE_NONE;
    g_encrypted = g_bonded = false;
    g_rsp_sub = g_status_sub = false;
    g_mtu = 23;
}

int gap_event(ble_gap_event* ev, void*) {
    std::lock_guard lk{g_mx};
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            g_advertising = false;
            if (ev->connect.status != 0) {
                advertise_locked();
                return 0;
            }
            if (!g_up) {  // RADIO_OFF raced the connection in
                (void)::ble_gap_terminate(ev->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            g_conn = ev->connect.conn_handle;
            CLK_LOGI(net, "ble: connected (handle %u)", g_conn);
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            CLK_LOGI(net, "ble: disconnected (reason 0x%03x)", ev->disconnect.reason);
            clear_link_locked();
            advertise_locked();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            g_advertising = false;
            advertise_locked();
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE: {
            ble_gap_conn_desc d{};
            if (ev->enc_change.status != 0 ||
                ::ble_gap_conn_find(ev->enc_change.conn_handle, &d) != 0) {
                CLK_LOGW(net, "ble: encryption failed (status %d)", ev->enc_change.status);
                return 0;
            }
            g_encrypted = d.sec_state.encrypted;
            g_bonded = d.sec_state.bonded;
            if (g_encrypted && !g_bonded) {
                // A stranger ran Just Works while the window was shut: encrypted, not stored.
                ++g_refused;
                CLK_LOGW(net, "ble: pairing refused -- the window is closed (hold the knob 10 s)");
                (void)::ble_gap_terminate(ev->enc_change.conn_handle, BLE_ERR_AUTH_FAIL);
                return 0;
            }
            g_bonds = count_bonds();
            // Counted as a new bond whenever a bonded link comes up INSIDE the window.  A phone
            // that was already bonded and reconnects during it counts too, which ends the
            // window the same way -- the phone the user is holding works, and that is the
            // answer they are waiting for.
            if (g_bonded && g_pairable) {
                ++g_paired;
                CLK_LOGI(net, "ble: bonded (%u in the store)", g_bonds);
            }
            return 0;
        }

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // The phone forgot us and we did not forget it.  Inside the window: drop the old
            // bond and let it pair again.  Outside: refuse -- re-pairing IS pairing.
            if (!g_pairable) return BLE_GAP_REPEAT_PAIRING_IGNORE;
            ble_gap_conn_desc d{};
            if (::ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) {
                (void)::ble_store_util_delete_peer(&d.peer_id_addr);
            }
            g_bonds = count_bonds();
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (ev->subscribe.attr_handle == g_rsp_handle) g_rsp_sub = ev->subscribe.cur_notify;
            if (ev->subscribe.attr_handle == g_status_handle)
                g_status_sub = ev->subscribe.cur_notify;
            return 0;

        case BLE_GAP_EVENT_MTU:
            g_mtu = ev->mtu.value;
            return 0;

        default:
            return 0;
    }
}

void on_sync() {
    int rc = ::ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ::ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        CLK_LOGE(net, "ble: no identity address (rc=%d)", rc);
        return;
    }
    std::lock_guard lk{g_mx};
    g_synced = true;
    g_bonds = count_bonds();
    advertise_locked();
}

void on_reset(int reason) {
    CLK_LOGW(net, "ble: host reset (reason %d)", reason);
    std::lock_guard lk{g_mx};
    g_synced = false;
    g_advertising = false;
    clear_link_locked();
}

void host_task(void*) {
    ::nimble_port_run();  // returns only on nimble_port_stop()
    ::nimble_port_freertos_deinit();
}

}  // namespace

Status start(const char* name, RxFn on_cmd) noexcept {
    {
        std::lock_guard lk{g_mx};
        g_rx = on_cmd;
        if (name) std::snprintf(g_name, sizeof g_name, "%s", name);
        g_up = true;
        if (g_inited) {  // coming back from stop(): the stack is still there
            advertise_locked();
            return Status::Ok;
        }
    }

    if (const esp_err_t e = ::nimble_port_init(); e != ESP_OK) {
        CLK_LOGE(net, "ble: nimble_port_init %d", static_cast<int>(e));
        std::lock_guard lk{g_mx};
        g_up = false;
        return Status::Failed;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;  // full store: evict the oldest
    // LE Secure Connections, Just Works, bonded.  No MITM: there is no display and no keypad
    // -- the proximity proof is the ten-second hold that opened the window (§8.2).
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;  // the window starts shut
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ::ble_svc_gap_init();
    ::ble_svc_gatt_init();
    int rc = ::ble_gatts_count_cfg(kSvcs);
    if (rc == 0) rc = ::ble_gatts_add_svcs(kSvcs);
    if (rc == 0) rc = ::ble_svc_gap_device_name_set(g_name);
    if (rc != 0) {
        CLK_LOGE(net, "ble: GATT registration rc=%d", rc);
        std::lock_guard lk{g_mx};
        g_up = false;
        return Status::Failed;
    }
    ::ble_store_config_init();
    {
        std::lock_guard lk{g_mx};
        g_inited = true;
    }
    ::nimble_port_freertos_init(host_task);
    CLK_LOGI(net, "ble: stack up as \"%s\"", g_name);
    return Status::Ok;
}

Status stop() noexcept {
    std::lock_guard lk{g_mx};
    g_up = false;
    g_pairable = false;
    ble_hs_cfg.sm_bonding = 0;
    if (!g_inited) return Status::Ok;
    if (g_advertising) {
        (void)::ble_gap_adv_stop();
        g_advertising = false;
    }
    if (g_conn != BLE_HS_CONN_HANDLE_NONE) {
        (void)::ble_gap_terminate(g_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    return Status::Ok;
}

Status set_pairable(bool on) noexcept {
    std::lock_guard lk{g_mx};
    if (!g_up) return Status::NotReady;
    if (g_pairable == on) return Status::Ok;
    g_pairable = on;
    ble_hs_cfg.sm_bonding = on ? 1 : 0;
    advertise_locked();  // new interval + the manufacturer byte
    return Status::Ok;
}

Status notify_rsp(const uint8_t* data, std::size_t len) noexcept {
    uint16_t conn;
    {
        std::lock_guard lk{g_mx};
        if (g_conn == BLE_HS_CONN_HANDLE_NONE || !g_bonded || !g_rsp_sub) return Status::NotReady;
        if (len > static_cast<std::size_t>(g_mtu - 3)) return Status::BadArg;
        conn = g_conn;
    }
    os_mbuf* om = ::ble_hs_mbuf_from_flat(data, static_cast<uint16_t>(len));
    if (!om) return Status::Busy;
    // Consumes `om` whatever it returns.
    const int rc = ::ble_gatts_notify_custom(conn, g_rsp_handle, om);
    if (rc == 0) return Status::Ok;
    return rc == BLE_HS_ENOMEM ? Status::Busy : Status::Failed;
}

Status set_status(const uint8_t* data, std::size_t len, bool notify) noexcept {
    uint16_t conn;
    {
        std::lock_guard lk{g_mx};
        if (len > kStatusCap) return Status::BadArg;
        std::memcpy(g_status, data, len);
        g_status_len = len;
        if (!notify) return Status::Ok;
        if (g_conn == BLE_HS_CONN_HANDLE_NONE || !g_bonded || !g_status_sub)
            return Status::NotReady;
        if (len > static_cast<std::size_t>(g_mtu - 3)) return Status::BadArg;  // readable still
        conn = g_conn;
    }
    os_mbuf* om = ::ble_hs_mbuf_from_flat(data, static_cast<uint16_t>(len));
    if (!om) return Status::Busy;
    const int rc = ::ble_gatts_notify_custom(conn, g_status_handle, om);
    if (rc == 0) return Status::Ok;
    return rc == BLE_HS_ENOMEM ? Status::Busy : Status::Failed;
}

Status set_info(const char* text) noexcept {
    std::lock_guard lk{g_mx};
    std::snprintf(g_info, sizeof g_info, "%s", text ? text : "");
    return Status::Ok;
}

Status unbond_all() noexcept {
    uint16_t conn;
    {
        std::lock_guard lk{g_mx};
        if (!g_inited) return Status::NotReady;
        conn = g_conn;
    }
    if (conn != BLE_HS_CONN_HANDLE_NONE)
        (void)::ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    const int rc = ::ble_store_clear();
    std::lock_guard lk{g_mx};
    g_bonds = count_bonds();
    return rc == 0 ? Status::Ok : Status::Failed;
}

Link link() noexcept {
    std::lock_guard lk{g_mx};
    return Link{g_up,        g_advertising, g_pairable, g_conn != BLE_HS_CONN_HANDLE_NONE,
                g_encrypted, g_bonded,      g_rsp_sub,  g_status_sub,
                g_mtu,       g_bonds,       g_paired,   g_refused};
}

}  // namespace clk::hal::ble
