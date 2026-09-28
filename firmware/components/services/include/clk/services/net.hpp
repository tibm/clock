// The app link: BLE today, Wi-Fi when it lands.                [FIRMWARE.md §6.7, §8]
//
// Three jobs, and no product logic in any of them:
//
//   1. The PAIRING WINDOW.  Opened by the ten-second knob hold (`ui` calls pair()) or by
//      `net ble pair`; closed by a bond, by the knob, or by `window_ms` running out.  Only
//      while it is open can a phone bond -- the hold is the proximity proof (§8.2).
//   2. The COMMAND CHANNEL.  A bonded phone writes a CLI line; this AO runs it through the
//      very same dispatcher the console uses and streams the output back.  The app has the
//      console's command set, no more and no less (rule 6).
//   3. The STATUS SNAPSHOT.  Every `period_ms` it gathers what every service knows into one
//      transport::Snapshot, serves it on `status`, and notifies a subscriber.
//
// RADIO_OFF (expander GPA3) is a hard override, polled here: asserted, the stack goes quiet
// and the window cannot open.
#pragma once

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
    uint16_t seq_ = 0;

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
