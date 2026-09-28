// The fake BLE stack: hal::ble's contract, with a function call where the radio would be.
// [FIRMWARE.md D14, §8.2]
//
// What is modelled is what the `net` AO branches on: whether a link exists, whether it is
// bonded, whether the central subscribed, how big a notification may be, and that the
// pairing window is the only way to get a bond.  The access check (bonded or refused) is
// the same rule ble_esp.cpp enforces, so a test that passes here is testing the policy that
// ships.  There is no timing, no mbuf pool beyond a bounded queue, and no RF.
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/log.hpp"

namespace clk::hal {
namespace {

// Deeper than any single command's output needs, shallow enough that a test which forgets
// to drain sees Busy rather than an unbounded vector.
constexpr std::size_t kQueueDepth = 512;

struct Fake {
    bool up = false;
    bool pairable = false;
    bool connected = false;
    bool encrypted = false;
    bool bonded_link = false;
    bool rsp_sub = false, status_sub = false;
    uint16_t mtu = 23;
    uint8_t bonds = 0;
    uint32_t paired = 0, refused = 0;
    uint32_t status_notifies = 0;
    ble::RxFn rx = nullptr;
    ble::BlobFn blob = nullptr;
    std::deque<std::string> rsp;
    std::vector<uint8_t> status;
    std::string info;
};

std::mutex g_mx;
Fake g;

void drop_link_locked() {
    g.connected = g.encrypted = g.bonded_link = false;
    g.rsp_sub = g.status_sub = false;
    g.mtu = 23;
}

}  // namespace

namespace ble {

Status start(const char* name, RxFn on_cmd) noexcept {
    std::lock_guard lk{g_mx};
    g.up = true;
    g.rx = on_cmd;
    CLK_LOGI(net, "ble: fake stack up as \"%s\"", name ? name : "?");
    return Status::Ok;
}

void set_blob_handler(BlobFn fn) noexcept {
    std::lock_guard lk{g_mx};
    g.blob = fn;
}

Status stop() noexcept {
    std::lock_guard lk{g_mx};
    g.up = false;
    g.pairable = false;
    drop_link_locked();
    return Status::Ok;
}

Status set_pairable(bool on) noexcept {
    std::lock_guard lk{g_mx};
    if (!g.up) return Status::NotReady;
    g.pairable = on;
    return Status::Ok;
}

Status notify_rsp(const uint8_t* data, std::size_t len) noexcept {
    std::lock_guard lk{g_mx};
    if (!g.bonded_link || !g.rsp_sub) return Status::NotReady;
    if (len > static_cast<std::size_t>(g.mtu - 3)) return Status::BadArg;
    if (g.rsp.size() >= kQueueDepth) return Status::Busy;
    g.rsp.emplace_back(reinterpret_cast<const char*>(data), len);
    return Status::Ok;
}

Status set_status(const uint8_t* data, std::size_t len, bool notify) noexcept {
    std::lock_guard lk{g_mx};
    g.status.assign(data, data + len);
    if (!notify) return Status::Ok;
    if (!g.bonded_link || !g.status_sub) return Status::NotReady;
    ++g.status_notifies;
    return Status::Ok;
}

Status set_info(const char* text) noexcept {
    std::lock_guard lk{g_mx};
    g.info = text ? text : "";
    return Status::Ok;
}

Status unbond_all() noexcept {
    std::lock_guard lk{g_mx};
    g.bonds = 0;
    if (g.bonded_link) drop_link_locked();
    return Status::Ok;
}

Link link() noexcept {
    std::lock_guard lk{g_mx};
    return Link{
        g.up,      g.up && !g.connected, g.pairable, g.connected, g.encrypted, g.bonded_link,
        g.rsp_sub, g.status_sub,         g.mtu,      g.bonds,     g.paired,    g.refused};
}

}  // namespace ble

namespace host {

void ble_connect() noexcept {
    std::lock_guard lk{g_mx};
    if (!g.up || g.connected) return;
    g.connected = true;
}

bool ble_pair() noexcept {
    std::lock_guard lk{g_mx};
    if (!g.connected) return false;
    // The rule ble_esp.cpp implements with sm_bonding + the ENC_CHANGE check: outside the
    // window the link may encrypt but cannot bond, and an unbonded encrypted link is dropped.
    if (!g.pairable) {
        ++g.refused;
        drop_link_locked();
        return false;
    }
    g.encrypted = g.bonded_link = true;
    ++g.bonds;
    ++g.paired;
    return true;
}

void ble_reconnect() noexcept {
    std::lock_guard lk{g_mx};
    if (!g.up || g.connected || g.bonds == 0) return;
    g.connected = g.encrypted = g.bonded_link = true;
}

void ble_disconnect() noexcept {
    std::lock_guard lk{g_mx};
    drop_link_locked();
    g.rsp.clear();
}

void ble_subscribe(bool rsp, bool status) noexcept {
    std::lock_guard lk{g_mx};
    if (!g.bonded_link) return;  // the CCCD needs the same security as the value
    g.rsp_sub = rsp;
    g.status_sub = status;
}

void ble_set_mtu(uint16_t mtu) noexcept {
    std::lock_guard lk{g_mx};
    if (g.connected) g.mtu = mtu < 23 ? 23 : (mtu > 517 ? 517 : mtu);
}

Status ble_write(const char* text) noexcept {
    ble::RxFn rx = nullptr;
    {
        std::lock_guard lk{g_mx};
        if (!g.bonded_link) return Status::Denied;
        rx = g.rx;
    }
    const std::size_t n = std::strlen(text);
    if (n > ble::kMaxWrite) return Status::BadArg;  // ATT "invalid attribute value length"
    if (rx) rx(reinterpret_cast<const uint8_t*>(text), n);
    return Status::Ok;
}

uint8_t ble_write_blob(const uint8_t* data, std::size_t len) noexcept {
    ble::BlobFn fn = nullptr;
    {
        std::lock_guard lk{g_mx};
        if (!g.bonded_link) return 0x05;  // ATT insufficient authentication
        fn = g.blob;
    }
    if (len <= 4 || len > ble::kMaxBlob) return 0x0D;  // invalid attribute value length
    if (!fn) return ble::kBlobErrNoUpload;
    return ble::blob_att_err(fn(data, len));
}

bool ble_pop_rsp(char* out, std::size_t cap) noexcept {
    std::lock_guard lk{g_mx};
    if (g.rsp.empty() || cap == 0) return false;
    const std::string& f = g.rsp.front();
    const std::size_t n = f.size() < cap - 1 ? f.size() : cap - 1;
    std::memcpy(out, f.data(), n);
    out[n] = '\0';
    g.rsp.pop_front();
    return true;
}

std::size_t ble_read_status(uint8_t* out, std::size_t cap) noexcept {
    std::lock_guard lk{g_mx};
    const std::size_t n = g.status.size() < cap ? g.status.size() : cap;
    if (n) std::memcpy(out, g.status.data(), n);
    return n;
}

uint32_t ble_status_notifies() noexcept {
    std::lock_guard lk{g_mx};
    return g.status_notifies;
}

std::size_t ble_read_info(char* out, std::size_t cap) noexcept {
    std::lock_guard lk{g_mx};
    if (cap == 0) return 0;
    const std::size_t n = g.info.size() < cap - 1 ? g.info.size() : cap - 1;
    std::memcpy(out, g.info.data(), n);
    out[n] = '\0';
    return n;
}

}  // namespace host

// Heap numbers mean nothing on a laptop; zero says "not measured" without inventing one.
namespace sys {
Info info() noexcept { return Info{0, 0, 0}; }
}  // namespace sys

}  // namespace clk::hal
