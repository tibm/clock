#include "clk/services/net.hpp"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "clk/log.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"
#include "clk/services/ui.hpp"

namespace clk::svc {
namespace {

constexpr uint32_t kTickMs = 50;
constexpr uint32_t kRadioEveryMs = 1000;  // the rear toggle is a switch, not a trigger
// How long a phone's command waits for the CLI before it is told `busy`.  Long enough to
// queue behind an ordinary command; far shorter than a console stream (§9.5), which is the
// case it exists for.
constexpr uint32_t kDispatchWaitMs = 1500;
// A notification that finds no buffer is retried this often, this many times, before the
// frame is counted lost -- a burst like `help` outruns the controller's queue at 1 Hz
// connection intervals, and it drains in tens of milliseconds.
constexpr uint32_t kNotifyRetryMs = 10;
constexpr int kNotifyRetries = 100;
constexpr uint32_t kEnvEveryMs = 60'000;
constexpr uint32_t kAlsEveryMs = 5'000;
constexpr uint32_t kMinPeriodMs = 100;
constexpr uint32_t kMaxPeriodMs = 3'600'000;
constexpr uint32_t kMinWindowMs = 1000;
constexpr uint32_t kMaxWindowMs = 600'000;

template <class T>
T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int16_t sat16(float v) {
    if (!(v == v)) return 0;  // NaN
    return static_cast<int16_t>(clamp(std::lround(v), -32767L, 32767L));
}

uint16_t age_s(bool read, uint64_t at_us, uint64_t now_us) {
    if (!read) return transport::kAgeNever;
    const uint64_t s = (now_us - at_us) / 1'000'000ull;
    return static_cast<uint16_t>(s >= transport::kAgeNever ? transport::kAgeNever - 1 : s);
}

// A Sink that frames each record into notifications (transport/frame.hpp).  Once one frame
// cannot be delivered the rest of the answer is pointless -- the phone has gone, or stopped
// listening -- so it goes quiet rather than retrying every line.
class BleSink final : public cmd::Sink {
public:
    // `retries` is 0 on the BLE host task: the buffers a retry waits for are freed by that
    // very task, so waiting there only ever times out.
    explicit BleSink(uint16_t id, int retries = kNotifyRetries) noexcept
        : id_(id), retries_(retries) {}

    void line(const char* t) override { put(transport::Kind::Line, t); }
    void kv(const char* k, const char* v) override {
        char buf[transport::kMaxLine];
        std::snprintf(buf, sizeof buf, "%s=%s", k, v);
        put(transport::Kind::Kv, buf);
    }
    void done(Status s) override { put(transport::Kind::Done, clk::name(s)); }

    uint32_t lost = 0;

private:
    static bool emit(void* ctx, const uint8_t* d, std::size_t n) {
        auto* self = static_cast<BleSink*>(ctx);
        for (int i = 0;; ++i) {
            const Status st = hal::ble::notify_rsp(d, n);
            if (st == Status::Ok) return true;
            if (st != Status::Busy || i >= self->retries_) break;
            hal::clock_::sleep_ms(kNotifyRetryMs);
        }
        self->dead_ = true;
        ++self->lost;
        return false;
    }
    void put(transport::Kind k, const char* text) {
        if (dead_) return;
        const auto l = hal::ble::link();
        const std::size_t frame = l.mtu > 3 ? l.mtu - 3u : 20u;
        transport::emit_record(id_, k, text, frame, &BleSink::emit, this);
    }

    uint16_t id_;
    int retries_;
    bool dead_ = false;
};

}  // namespace

Net::Net() noexcept : ActiveObject({"net", 6, 8192, kTickMs}) {}

Net& net() noexcept {
    static Net n;
    return n;
}

void Net::set_identity(const char* name, const char* info, uint32_t fw_id) noexcept {
    port::Lock lk{mx_};
    if (name) std::snprintf(name_, sizeof name_, "%s", name);
    if (info) std::snprintf(info_, sizeof info_, "%s", info);
    fw_id_ = fw_id;
}

void Net::on_start() {
    {
        port::Lock lk{mx_};
        snap_.window_ms = snap_.window_ms ? snap_.window_ms : kDefaultWindowMs;
        snap_.period_ms = snap_.period_ms ? snap_.period_ms : kDefaultPeriodMs;
    }
    (void)hal::ble::set_info(info_);
    poll_radio();  // starts the stack unless the rear toggle says no
    take_status();
    publish();
}

// ---- the pairing window ------------------------------------------------------------------

// Done here, on the caller's thread, snapshot AND radio -- not posted.  This AO's thread can
// be inside a slow sensor read for a second (take_status), and the blue row must never
// promise a window the radio has not opened yet, nor keep one open after the knob shut it.
// hal::ble is callable from any task by contract.
bool Net::pair(bool open) noexcept {
    {
        port::Lock lk{mx_};
        if (open) {
            if (!up_) return false;  // radio off, or no radio: nothing to open
            if (snap_.pairing) return true;
            const uint32_t w = snap_.window_ms ? snap_.window_ms : kDefaultWindowMs;
            pair_until_us_ = port::now_us() + w * 1000ull;
            snap_.pairing = true;
            snap_.pair_left_ms = w;
            paired_seen_ = hal::ble::link().paired;
        } else if (!end_locked(PairEnd::Cancelled)) {
            return true;  // already shut; do not overwrite how it ended
        }
    }
    (void)hal::ble::set_pairable(open);
    CLK_LOGI(net, "pairing window %s", open ? "open" : "closed: cancelled");
    return true;
}

bool Net::end_locked(PairEnd why) noexcept {
    if (!snap_.pairing) return false;
    snap_.pairing = false;
    snap_.pair_left_ms = 0;
    snap_.last_end = why;
    ++snap_.windows;
    pair_until_us_ = 0;
    return true;
}

void Net::close_window(PairEnd why) noexcept {
    {
        port::Lock lk{mx_};
        if (!end_locked(why)) return;
    }
    (void)hal::ble::set_pairable(false);
    static constexpr const char* kWhy[] = {"", "bonded", "timed out", "cancelled", "radio off"};
    CLK_LOGI(net, "pairing window closed: %s", kWhy[static_cast<int>(why)]);
}

void Net::set_window_ms(uint32_t ms) noexcept {
    port::Lock lk{mx_};
    snap_.window_ms = clamp(ms, kMinWindowMs, kMaxWindowMs);
}

void Net::set_period_ms(uint32_t ms) noexcept {
    port::Lock lk{mx_};
    snap_.period_ms = clamp(ms, kMinPeriodMs, kMaxPeriodMs);
}

// ---- events ------------------------------------------------------------------------------

void Net::on_event(Event const& e) {
    if (as<NetRx>(e)) return run_queued();
    if (as<NetUnbond>(e)) {
        const Status st = hal::ble::unbond_all();
        CLK_LOGI(net, "unbond: %s", clk::name(st));
    }
}

void Net::on_tick() {
    const uint64_t now = port::now_us();
    if (now - radio_at_us_ >= kRadioEveryMs * 1000ull) poll_radio();

    const auto l = hal::ble::link();
    bool open, bonded;
    uint64_t until;
    uint32_t period;
    {
        port::Lock lk{mx_};
        open = snap_.pairing;
        until = pair_until_us_;
        period = snap_.period_ms;
        bonded = l.paired != paired_seen_;
        paired_seen_ = l.paired;
    }
    if (open && bonded) {
        close_window(PairEnd::Bonded);
    } else if (open && now >= until) {
        close_window(PairEnd::Expired);
    }
    if (l.refused != refused_seen_) {
        refused_seen_ = l.refused;
        CLK_LOGW(net, "a phone tried to pair with the window shut -- hold the knob 10 s first");
    }

    if (now - status_at_us_ >= period * 1000ull) {
        status_at_us_ = now;
        take_status();
    }
    publish();
}

// RADIO_OFF idles HIGH through the expander pull-up and the toggle pulls it LOW.  An
// unreadable expander fails to radios-ON, which is what the harness does too (README §16d)
// -- a clock whose app link dies with its expander is a clock nobody can debug remotely.
void Net::poll_radio() noexcept {
    radio_at_us_ = port::now_us();
    const auto r = hal::expander::get(hal::expander::Sig::RadioOff);
    const bool off = r.ok() && !r.v;
    // Once per edge of the toggle -- and once at boot.  A radio that will not start is told
    // so once, not every second (D16).
    if (off == radio_off_ && tried_) return;
    radio_off_ = off;
    tried_ = true;
    if (off) {
        if (up_) CLK_LOGI(net, "RADIO_OFF: BLE stopped");
        close_window(PairEnd::Refused);
        (void)hal::ble::stop();
        port::Lock lk{mx_};
        up_ = false;
        return;
    }
    const Status st = hal::ble::start(name_, &Net::on_rx);
    if (st != Status::Ok) CLK_LOGW(net, "BLE did not start: %s", clk::name(st));
    port::Lock lk{mx_};
    up_ = st == Status::Ok;
}

// ---- the command channel -----------------------------------------------------------------

// BLE host task.  Copy, wake the AO, return -- or, with the queue full, answer `busy` from
// right here, because a write that is silently dropped leaves the phone waiting forever.
void Net::on_rx(const uint8_t* data, std::size_t len) noexcept {
    Net& self = net();
    if (len == 0 || len > hal::ble::kMaxWrite) return;
    bool queued = false;
    {
        port::Lock lk{self.rx_mx_};
        if (self.rx_count_ < kRxDepth) {
            RxSlot& s = self.rxq_[(self.rx_head_ + self.rx_count_) % kRxDepth];
            std::memcpy(s.data, data, len);
            s.len = static_cast<uint16_t>(len);
            ++self.rx_count_;
            queued = true;
        }
    }
    if (queued) {
        self.post(NetRx{});
        return;
    }
    transport::Request rq;
    if (!transport::parse_request(data, len, rq)) rq.id = 0;
    BleSink sink{rq.id, 0};
    sink.line("busy: too many commands in flight");
    sink.done(Status::Busy);
    port::Lock lk{self.mx_};
    ++self.snap_.busy;
}

void Net::run_queued() noexcept {
    for (;;) {
        RxSlot s;
        {
            port::Lock lk{rx_mx_};
            if (rx_count_ == 0) return;
            s = rxq_[rx_head_];
            rx_head_ = (rx_head_ + 1) % kRxDepth;
            --rx_count_;
        }
        transport::Request rq;
        const bool ok = transport::parse_request(s.data, s.len, rq);
        BleSink sink{rq.id};
        Status st;
        if (!ok) {
            sink.line("bad request: want \"<id> <command>\", at most 256 bytes");
            sink.done(Status::BadArg);
            st = Status::BadArg;
        } else if (!dispatch_) {
            sink.line("no command dispatcher bound");
            sink.done(Status::NotPresent);
            st = Status::NotPresent;
        } else {
            CLK_LOGD(net, "ble cmd %u: %s", rq.id, rq.line);
            st = dispatch_(rq.line, sink, kDispatchWaitMs);
        }
        port::Lock lk{mx_};
        ++snap_.cmds;
        if (st == Status::Busy) ++snap_.busy;
        snap_.lost += sink.lost;
    }
}

// ---- the status snapshot -----------------------------------------------------------------

transport::BleState Net::ble_state(hal::ble::Link const& l) const noexcept {
    using transport::BleState;
    if (!l.up) return BleState::Off;
    if (l.connected) return l.bonded ? BleState::Secure : BleState::Connected;
    return l.pairable ? BleState::Pairing : BleState::Idle;
}

void Net::take_status() noexcept {
    using namespace transport;
    const uint64_t now = port::now_us();
    transport::Snapshot s{};
    s.seq = ++seq_;
    s.uptime_s = static_cast<uint32_t>(now / 1'000'000ull);
    s.clk_src = static_cast<uint8_t>(hal::clock_::slow_src());
    const auto si = hal::sys::info();
    s.heap_free = si.heap_free;
    s.heap_min = si.heap_min;
    s.reset_reason = si.reset_reason;
    {
        port::Lock lk{mx_};
        s.fw_id = fw_id_;
    }
    uint32_t f = 0;

    if (chrono_) {
        const auto c = chrono_->snapshot();
        if (c.valid) {
            f |= kTimeValid;
            s.epoch_ms = c.epoch_ms;
        }
        if (c.follow) f |= kTimeFollow;
        if (c.net_provisioned) f |= kNetProvisioned;
        if (c.net_synced) f |= kNetSynced;
    }

    if (motion_) {
        const auto m = motion_->snapshot();
        s.motion_state = static_cast<uint8_t>(m.state);
        s.dial_tick = m.dial_tick;
        s.hand_h = static_cast<uint8_t>(m.hour);
        s.hand_m = static_cast<uint8_t>(m.minute);
        s.target_h = static_cast<uint8_t>(m.target_hour);
        s.target_m = static_cast<uint8_t>(m.target_minute);
        s.opto = static_cast<uint16_t>(clamp(m.opto, 0.0f, 1.0f) * 65535.0f + 0.5f);
        s.motion_faults = m.faults;
        s.trims = static_cast<uint16_t>(m.trims);
        s.last_trim = static_cast<int16_t>(clamp<int32_t>(m.last_trim, -32767, 32767));
        if (m.homed) f |= kHomed;
        if (m.powered) f |= kMotorPowered;
    }

    // Power and gravity come from `ui`, which already polls both on its own tick: asking the
    // chips a second time from a second task is extra bus traffic and a race in a driver that
    // was written for one caller.  (Both move to `board` together, §6.5.)
    if (ui_) {
        const auto u = ui_->snapshot();
        s.ui_mode = static_cast<uint8_t>(u.mode);
        s.volume = u.volume;
        s.alarm_h = static_cast<uint8_t>(u.alarm_hour);
        s.alarm_m = static_cast<uint8_t>(u.alarm_minute);
        s.brightness = u.brightness;
        s.knob_count = u.knob_count;
        if (u.held_ms > 0) f |= kKnobPressed;
        if (u.input) f |= kKnobInput;
        if (u.alarm_armed) f |= kAlarmArmed;
        if (u.net_locked) f |= kNetLocked;
        if (u.power_ok) {
            f |= kPowerOk;
            s.vbat_mv = u.power.vbat_mv;
            s.soc_pct = u.power.soc_pct;
            s.vbat_src = static_cast<uint8_t>(u.power.src);
            if (u.power.plugged) f |= kPlugged;
            if (u.power.charging) f |= kCharging;
            if (u.power.fault) f |= kChargeFault;
        }
        if (u.batt_warn) f |= kBattLow;
        if (u.imu_ok) {
            f |= kImuOk;
            s.grav_mm[0] = sat16(u.imu.gx * 1000.0f);
            s.grav_mm[1] = sat16(u.imu.gy * 1000.0f);
            s.grav_mm[2] = sat16(u.imu.gz * 1000.0f);
            s.ypr_cdeg[0] = sat16(u.imu.yaw_deg * 100.0f);
            s.ypr_cdeg[1] = sat16(u.imu.pitch_deg * 100.0f);
            s.ypr_cdeg[2] = sat16(u.imu.roll_deg * 100.0f);
            s.taps = u.imu.taps;
        }
    }
    if (hal::imu::link().ready) f |= kImuLink;
    if (const auto fc = hal::power::full_charge(); fc.ok() && fc.v) f |= kFullCharge;

    // The room, on its own slow cadence.
    if (!env_read_ || now - env_at_us_ >= kEnvEveryMs * 1000ull) {
        env_at_us_ = now;
        const auto e = hal::env::read();
        env_read_ = e.ok();
        if (e.ok()) {
            room_.temp_cdeg = sat16(e.v.temp_c * 100.0f);
            room_.rh_cpct = static_cast<uint16_t>(clamp(e.v.rh_pct, 0.0f, 100.0f) * 100.0f + 0.5f);
            room_.press_dhpa =
                static_cast<uint16_t>(clamp(e.v.press_hpa, 0.0f, 6553.0f) * 10.0f + 0.5f);
            room_.gas_ohms = e.v.gas_ohms;
            room_.flags = (e.v.gas_valid ? uint32_t{kEnvGasValid} : 0u) |
                          (e.v.heat_stable ? uint32_t{kEnvHeatStable} : 0u);
        }
    }
    if (!als_read_ || now - als_at_us_ >= kAlsEveryMs * 1000ull) {
        als_at_us_ = now;
        const auto a = hal::als::read();
        als_read_ = a.ok();
        if (a.ok()) {
            room_.lux = a.v.lux;
            als_sat_ = a.v.saturated;
        }
    }
    if (env_read_) {
        f |= kEnvOk | (room_.flags & (kEnvGasValid | kEnvHeatStable));
        s.temp_cdeg = room_.temp_cdeg;
        s.rh_cpct = room_.rh_cpct;
        s.press_dhpa = room_.press_dhpa;
        s.gas_ohms = room_.gas_ohms;
    }
    s.env_age_s = age_s(env_read_, env_at_us_, now);
    if (als_read_) {
        f |= kAlsOk;
        if (als_sat_) f |= kAlsSaturated;
        s.lux = room_.lux;
    }
    s.als_age_s = age_s(als_read_, als_at_us_, now);

    // Light out: what was last WRITTEN to the chain, which is what the pixels show.
    for (std::size_t i = 0; i < kPixels && i < hal::pixels::kCount; ++i) {
        const auto p = hal::pixels::get(i);
        s.px[i][0] = p.r;
        s.px[i][1] = p.g;
        s.px[i][2] = p.b;
        s.px[i][3] = p.w;
    }
    s.wake_warm = hal::wake::warm();
    s.wake_cool = hal::wake::cool();
    if (hal::audio::active()) f |= kAmpActive;
    if (hal::audio::playing()) f |= kAudioPlaying;

    const auto l = hal::ble::link();
    s.ble_state = static_cast<uint8_t>(ble_state(l));
    s.bonds = l.bonds;
    if (radio_off_) f |= kRadioOff;
    if (l.connected) f |= kBleConnected;
    if (l.connected && l.bonded) f |= kBleSecure;
    if (l.pairable) f |= kBlePairing;
    s.flags = f;

    uint8_t wire[kWireSize];
    const std::size_t n = encode(s, wire, sizeof wire);
    {
        port::Lock lk{mx_};
        status_ = s;
    }
    if (n) (void)hal::ble::set_status(wire, n, l.bonded && l.status_sub);
}

void Net::publish() noexcept {
    const auto l = hal::ble::link();
    const uint64_t now = port::now_us();
    port::Lock lk{mx_};
    snap_.ble = ble_state(l);
    snap_.radio_off = radio_off_;
    snap_.link = l;
    snap_.pair_left_ms = snap_.pairing && pair_until_us_ > now
                             ? static_cast<uint32_t>((pair_until_us_ - now) / 1000)
                             : 0;
}

Net::Snapshot Net::snapshot() const noexcept {
    port::Lock lk{mx_};
    return snap_;
}

transport::Snapshot Net::status() const noexcept {
    port::Lock lk{mx_};
    return status_;
}

}  // namespace clk::svc
