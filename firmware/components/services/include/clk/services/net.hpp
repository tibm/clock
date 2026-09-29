// The app link over BLE, and Wi-Fi for the time.               [FIRMWARE.md §6.7, §8]
//
// Four jobs, and no product logic in any of them:
//
//   1. The PAIRING WINDOW.  Opened by the ten-second knob hold (`ui` calls pair()) or by
//      `net ble pair`; closed by a bond, by the knob, or by `window_ms` running out.  Only
//      while it is open can a phone bond -- the hold is the proximity proof (§8.2).
//   2. The COMMAND CHANNEL.  A bonded phone writes a CLI line; this AO runs it through the
//      very same dispatcher the console uses and streams the output back.  The app has the
//      console's command set, no more and no less (rule 6).
//   3. The STATUS SNAPSHOT.  Every `period_ms` it gathers what every service knows into one
//      transport::Snapshot, serves it on `status`, and notifies a subscriber.
//   4. WI-FI + SNTP.  The phone sends a network (`net wifi join`, over the bonded link); this
//      AO keeps it associated with backoff, and asks three public time servers in order --
//      the next one only when the one before gives no believable answer -- then hands UTC
//      to `chrono`.  Resync hourly.  (net_wifi.cpp)
//
// RADIO_OFF (expander GPA3) is a hard override, polled here: asserted, both radios go quiet
// and the window cannot open.
#pragma once

#include <cstddef>
#include <cstdint>

#include "clk/ao.hpp"
#include "clk/command/sink.hpp"
#include "clk/hal/hal.hpp"
#include "clk/transport/frame.hpp"
#include "clk/transport/snapshot.hpp"

namespace clk::svc {

class Motion;
class Chrono;
class Ui;
class Storage;

class Net final : public ActiveObject {
public:
    enum class PairEnd : uint8_t { None, Bonded, Expired, Cancelled, Refused };

    struct Snapshot {
        transport::BleState ble;
        bool radio_off;
        bool pairing;           // the window is open
        uint32_t pair_left_ms;  // ... for this much longer
        PairEnd last_end;       // how the last window closed
        uint32_t windows;       // +1 each time a window CLOSES -- `ui` watches this
        hal::ble::Link link;
        uint32_t cmds;  // command lines run
        uint32_t busy;  // ... answered `busy` (queue full, or the CLI was held)
        uint32_t lost;  // response frames that could not be delivered
        uint32_t window_ms;
        uint32_t period_ms;
    };

    // The dispatcher is injected, not linked: services/ sits below cli/ (§2), and the
    // console's line dispatcher is exactly what the app is meant to reach.
    using DispatchFn = Status (*)(char* line, cmd::Sink&, uint32_t wait_ms);

    static constexpr uint32_t kDefaultWindowMs = 120'000;
    static constexpr uint32_t kDefaultPeriodMs = 1000;

    Net() noexcept;

    // `s` receives the `blob` characteristic's upload data; without it every blob write is
    // refused "no upload open".
    void bind(Motion* m, Chrono* c, Ui* u, Storage* s = nullptr) noexcept {
        motion_ = m;
        chrono_ = c;
        ui_ = u;
        storage_ = s;
    }
    void set_dispatch(DispatchFn f) noexcept { dispatch_ = f; }
    // Advertised name, the `info` characteristic's text, and the build id stamped on every
    // snapshot.  Call before start().
    void set_identity(const char* name, const char* info, uint32_t fw_id) noexcept;

    // Open or close the pairing window.  Takes effect in the snapshot BEFORE it returns, so a
    // caller that looks straight afterwards sees what it asked for.  Opening returns false --
    // and opens nothing -- when the radio is off or there is no radio.
    bool pair(bool open) noexcept;
    void unbond() noexcept { post(NetUnbond{}); }
    void set_window_ms(uint32_t ms) noexcept;
    void set_period_ms(uint32_t ms) noexcept;

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // ---- Wi-Fi + SNTP (net_wifi.cpp) ----
    // Free public servers, tried in this order every round; the next only when the one
    // before times out, does not resolve, or answers with something not to be believed.
    static constexpr const char* kSntpServers[] = {"time.cloudflare.com", "time.google.com",
                                                   "pool.ntp.org"};
    static constexpr std::size_t kSntpCount = 3;

    struct Wifi {
        transport::WifiState state;
        hal::wifi::Link link;
        bool provisioned;  // a network is stored
        char ssid[hal::wifi::kSsidMax + 1];
        transport::WifiErr err;  // why the last attempt failed
        uint8_t reason;          // ... in the driver's words
        uint32_t attempts;       // association attempts since the network was set
        uint32_t retry_in_ms;    // Backoff: until the next attempt
        // SNTP
        bool synced;               // SNTP has set the clock since boot
        int8_t server;             // index into kSntpServers of the last good answer, -1 none
        int8_t trying;             // the server being asked right now, -1 none
        uint32_t synced_ago_s;     // since the last good answer
        uint32_t next_sync_in_ms;  // 0 while a round is running or nothing is scheduled
        int64_t last_step_ms;      // what the last sync moved the clock by (0 = first set)
        uint32_t syncs, fails;     // good answers / whole rounds with none
        char last_fail[48];        // "time.google.com: timeout"
    };
    // Store the network (NVS, hex) and start associating.  An empty psk is an open network.
    // BadArg on a bad length; NotPresent when there is no persistent store to keep it in is
    // NOT an error -- it still connects, it just will not survive a reboot.
    Status wifi_join(const char* ssid, const char* psk) noexcept;
    Status wifi_forget() noexcept;  // forget the network, disconnect, stay Idle
    void sntp_now() noexcept;       // start a round at the next tick
    [[nodiscard]] Wifi wifi() const noexcept;

    // The last status record taken (every period_ms, subscriber or not).
    [[nodiscard]] transport::Snapshot status() const noexcept;

protected:
    void on_start() override;
    void on_event(Event const&) override;
    void on_tick() override;

private:
    static void on_rx(const uint8_t* data, std::size_t len) noexcept;      // BLE host task
    static Status on_blob(const uint8_t* data, std::size_t len) noexcept;  // ... likewise
    void run_queued() noexcept;
    void poll_radio() noexcept;
    void close_window(PairEnd) noexcept;
    bool end_locked(PairEnd) noexcept;  // mx_ held; false if the window was already shut
    void take_status() noexcept;
    void publish() noexcept;
    [[nodiscard]] transport::BleState ble_state(hal::ble::Link const&) const noexcept;
    // Wi-Fi (net_wifi.cpp) -- all on this AO's thread except where noted.
    void wifi_boot() noexcept;  // on_start: load the stored network
    void wifi_radio(bool on) noexcept;
    void wifi_tick(uint64_t now) noexcept;
    void wifi_connect(uint64_t now) noexcept;
    void wifi_fail(uint64_t now, transport::WifiErr, uint8_t reason) noexcept;
    void sntp_tick(uint64_t now) noexcept;
    void sntp_send(uint64_t now) noexcept;
    void sntp_round_failed(uint64_t now) noexcept;
    void report_net() noexcept;

    mutable port::Mutex mx_;
    Snapshot snap_{};
    transport::Snapshot status_{};

    Motion* motion_ = nullptr;
    Chrono* chrono_ = nullptr;
    Ui* ui_ = nullptr;
    Storage* storage_ = nullptr;
    DispatchFn dispatch_ = nullptr;

    char name_[24] = "clock";
    char info_[200] = "";
    uint32_t fw_id_ = 0;

    bool tried_ = false;  // the stack has been asked to start at least once
    bool radio_off_ = false;
    bool up_ = false;  // written under mx_: pair() reads it from other tasks
    uint64_t pair_until_us_ = 0;
    uint32_t paired_seen_ = 0;  // under mx_: pair() sets the baseline, the tick compares
    uint32_t refused_seen_ = 0;
    uint64_t radio_at_us_ = 0;
    uint64_t status_at_us_ = 0;
    uint64_t hist_at_us_ = 0;
    uint16_t seq_ = 0;

    // Wi-Fi.  wifi_ (and the pending flags) under mx_: the CLI thread writes them.
    Wifi wifi_{};
    char psk_[hal::wifi::kPskMax + 1] = "";  // under mx_; never printed, never logged
    bool join_pending_ = false, forget_pending_ = false, sntp_pending_ = false;
    bool wifi_up_ = false;  // hal::wifi started
    uint64_t attempt_at_us_ = 0, retry_at_us_ = 0;
    uint8_t backoff_ = 0;  // index into the backoff table
    // SNTP round
    bool sntp_busy_ = false;
    uint8_t sntp_idx_ = 0;
    uint8_t sntp_backoff_ = 0;
    uint64_t sntp_nonce_ = 0;
    uint64_t sync_at_us_ = 0, next_sync_us_ = 0;
    bool reported_ = false, reported_prov_ = false, reported_sync_ = false;

    // The slow sensors are sampled on their own cadence, not the snapshot's: the BME688
    // blocks ~200 ms and heats itself, and nobody's room changes temperature in a second.
    transport::Snapshot room_{};  // env + light fields only
    uint64_t env_at_us_ = 0, als_at_us_ = 0;
    bool env_read_ = false, als_read_ = false;
    bool als_sat_ = false;

    // Command lines waiting for this thread.  Small: a phone that sends four commands without
    // waiting for one answer is told `busy`, not queued behind a stream.
    static constexpr std::size_t kRxDepth = 4;
    struct RxSlot {
        uint16_t len;
        uint8_t data[hal::ble::kMaxWrite];
    };
    RxSlot rxq_[kRxDepth]{};
    std::size_t rx_head_ = 0, rx_count_ = 0;
    port::Mutex rx_mx_;
};

Net& net() noexcept;

}  // namespace clk::svc
