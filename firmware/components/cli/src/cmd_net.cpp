// The `net` group, and `sys snap`.                          [FIRMWARE.md §9.3, §8]
//
// Views and requests, like every other group: the pairing window belongs to `net` and is
// opened through `ui` so that the five blue pixels and the radio can never disagree about
// whether the clock is pairable.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "clk/cli/net_bind.hpp"
#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/services/net.hpp"
#include "clk/services/storage.hpp"
#include "clk/services/ui.hpp"
#include "clk/transport/snapshot.hpp"

namespace clk::cli {
namespace {

using svc::Net;
using transport::BleState;

const char* end_name(Net::PairEnd e) {
    switch (e) {
        case Net::PairEnd::Bonded:
            return "bonded";
        case Net::PairEnd::Expired:
            return "timed out";
        case Net::PairEnd::Cancelled:
            return "cancelled";
        case Net::PairEnd::Refused:
            return "radio off";
        default:
            return "-";
    }
}

bool wait_for(bool (*pred)(), uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
        if (pred()) return true;
        hal::clock_::sleep_ms(10);
    }
    return pred();
}

Status cmd_status(Args const&, Sink& out) {
    const auto n = svc::net().snapshot();
    const auto& l = n.link;
    out.printf("radio   %s%s", n.radio_off ? "OFF (rear toggle)" : "on",
               l.up ? "" : "   ble stack down");
    out.printf("ble     %s   bonds %u   mtu %u   sub rsp=%d status=%d", transport::name(n.ble),
               l.bonds, l.mtu, l.rsp_sub, l.status_sub);
    if (n.pairing) {
        out.printf("pairing OPEN, %" PRIu32 " s left", n.pair_left_ms / 1000);
    } else {
        out.printf("pairing shut   last window: %s", end_name(n.last_end));
    }
    out.printf("link    connected=%d encrypted=%d bonded=%d   paired %" PRIu32
               " since boot, refused %" PRIu32,
               l.connected, l.encrypted, l.bonded, l.paired, l.refused);
    out.printf("cmds    %" PRIu32 " run   %" PRIu32 " busy   %" PRIu32 " frames lost", n.cmds,
               n.busy, n.lost);
    out.printf("tuning  window %" PRIu32 " s   status every %" PRIu32 " ms", n.window_ms / 1000,
               n.period_ms);
    const auto w = svc::net().wifi();
    out.printf("wifi    %s%s%s%s", transport::name(w.state), w.provisioned ? "   \"" : "",
               w.provisioned ? w.ssid : "   no network stored", w.provisioned ? "\"" : "");
    return Status::Ok;
}

// ---- Wi-Fi --------------------------------------------------------------------------------

// "hex:<digits>" is the app's form -- any byte, no quoting to get wrong -- and anything else
// is taken literally (quote it on the console if it has spaces).
bool arg_text(const char* a, char* out, std::size_t cap) {
    if (!a) return false;
    if (std::strncmp(a, "hex:", 4) != 0) {
        if (std::strlen(a) >= cap) return false;
        std::snprintf(out, cap, "%s", a);
        return true;
    }
    const char* h = a + 4;
    std::size_t n = 0;
    auto nib = [](char c) {
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
               : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                      : -1;
    };
    for (; h[0] && h[1]; h += 2) {
        const int x = nib(h[0]), y = nib(h[1]);
        if (x < 0 || y < 0 || n + 1 >= cap || (x | y) == 0) return false;
        out[n++] = static_cast<char>((x << 4) | y);
    }
    out[n] = '\0';
    return *h == '\0';
}

void ip_text(uint32_t ip, char* out, std::size_t cap) {
    std::snprintf(out, cap, "%u.%u.%u.%u", static_cast<unsigned>(ip & 0xFF),
                  static_cast<unsigned>((ip >> 8) & 0xFF), static_cast<unsigned>((ip >> 16) & 0xFF),
                  static_cast<unsigned>(ip >> 24));
}

Status cmd_wifi(Args const& a, Sink& out) {
    if (a.count() > 0) {
        out.printf("unknown: net wifi %s   (status | join | forget | scan)", a.arg(0));
        return Status::BadArg;
    }
    const auto w = svc::net().wifi();
    const auto n = svc::net().snapshot();
    if (n.radio_off) {
        out.line("radio   OFF (rear toggle)");
    }
    if (w.provisioned) {
        out.printf("network \"%s\"   %" PRIu32 " attempt(s)", w.ssid, w.attempts);
    } else {
        out.line("network none stored   (`net wifi join <ssid> [<psk>]`, or from the app)");
    }
    char ip[20] = "-";
    if (w.link.ip) ip_text(w.link.ip, ip, sizeof ip);
    out.printf("state   %s%s%s", transport::name(w.state),
               w.state == transport::WifiState::Online ? "   " : "",
               w.state == transport::WifiState::Online ? ip : "");
    if (w.state == transport::WifiState::Online)
        out.printf("link    %d dBm   channel %u", w.link.rssi, w.link.channel);
    if (w.err != transport::WifiErr::None) {
        static constexpr const char* kHint[] = {"",
                                                "no such network in range",
                                                "wrong password",
                                                "no address from DHCP",
                                                "no answer from the access point",
                                                "see the reason code"};
        out.printf("last    failed: %s (%s, reason %u)", transport::name(w.err),
                   kHint[static_cast<int>(w.err)], w.reason);
    }
    if (w.state == transport::WifiState::Backoff)
        out.printf("retry   in %" PRIu32 " s", (w.retry_in_ms + 999) / 1000);
    // Machine-readable: app/PROTOCOL.md "Wi-Fi".
    out.kv("state", transport::name(w.state));
    out.kv("ssid", w.provisioned ? w.ssid : "");
    out.kv("err", transport::name(w.err));
    char v[16];
    std::snprintf(v, sizeof v, "%d", w.state == transport::WifiState::Online ? w.link.rssi : 0);
    out.kv("rssi", v);
    out.kv("ip", w.link.ip ? ip : "");
    out.kv("synced", w.synced ? "1" : "0");
    return Status::Ok;
}

Status cmd_wifi_join(Args const& a, Sink& out) {
    char ssid[hal::wifi::kSsidMax + 1], psk[hal::wifi::kPskMax + 1] = "";
    if (a.count() < 1 || a.count() > 2 || !arg_text(a.arg(0), ssid, sizeof ssid) || !ssid[0] ||
        (a.count() == 2 && !arg_text(a.arg(1), psk, sizeof psk))) {
        out.line("usage: net wifi join <ssid> [<password>]   (either may be hex:<utf8 bytes>)");
        out.line("  ssid 1..32 bytes; password 8..63 characters, or 64 hex digits; none = open");
        return Status::BadArg;
    }
    const Status st = svc::net().wifi_join(ssid, psk);
    std::memset(psk, 0, sizeof psk);
    if (st == Status::BadArg) {
        out.line("bad password length: 8..63 characters, or 64 hex digits (none = open)");
        return st;
    }
    if (st == Status::Failed) {
        out.line("could not store the network -- joining anyway, it will not survive a reboot");
    }
    if (svc::net().snapshot().radio_off) {
        out.printf("saved \"%s\" -- the radio is OFF (rear toggle); joins when it is back on",
                   ssid);
    } else {
        out.printf("saved \"%s\" -- joining; watch `net wifi` (or wifi_state in the snapshot)",
                   ssid);
    }
    return Status::Ok;
}

Status cmd_wifi_forget(Args const&, Sink& out) {
    const auto w = svc::net().wifi();
    (void)svc::net().wifi_forget();
    out.printf("forgot %s%s%s -- disconnected", w.provisioned ? "\"" : "",
               w.provisioned ? w.ssid : "(nothing stored)", w.provisioned ? "\"" : "");
    return Status::Ok;
}

// Blocking ~2-3 s on target.  `=ap=<rssi>/<open|secured>/<channel>/<ssid>` per network,
// strongest first; the SSID last because it may contain anything.
Status cmd_wifi_scan(Args const&, Sink& out) {
    hal::wifi::Ap aps[20];
    const auto r = hal::wifi::scan(aps, sizeof aps / sizeof aps[0]);
    if (!r.ok()) {
        out.line(r.st == Status::Busy ? "busy: joining a network -- try again in a few seconds"
                 : r.st == Status::NotReady ? "the Wi-Fi radio is off (rear toggle)"
                                            : "scan failed");
        return r.st;
    }
    out.printf("%zu network(s)", r.v);
    for (std::size_t i = 0; i < r.v; ++i) {
        out.printf("  %4d dBm  ch %2u  %-7s  %s", aps[i].rssi, aps[i].channel,
                   aps[i].open ? "open" : "secured", aps[i].ssid);
        char v[80];
        std::snprintf(v, sizeof v, "%d/%s/%u/%.32s", aps[i].rssi, aps[i].open ? "open" : "secured",
                      aps[i].channel, aps[i].ssid);
        out.kv("ap", v);
    }
    return Status::Ok;
}

Status cmd_sntp(Args const& a, Sink& out) {
    if (a.count() > 0) {
        out.printf("unknown: net sntp %s   (sync)", a.arg(0));
        return Status::BadArg;
    }
    const auto w = svc::net().wifi();
    for (std::size_t i = 0; i < Net::kSntpCount; ++i) {
        out.printf("server  %zu  %s%s%s", i + 1, Net::kSntpServers[i],
                   w.server == static_cast<int>(i) ? "   <- last answer" : "",
                   w.trying == static_cast<int>(i) ? "   asking now" : "");
    }
    if (w.synced) {
        out.printf("synced  %" PRIu32 " s ago   last step %+" PRId64 " ms   %" PRIu32
                   " sync(s), %" PRIu32 " failed round(s)",
                   w.synced_ago_s, w.last_step_ms, w.syncs, w.fails);
    } else {
        out.printf("synced  never since boot   %" PRIu32 " failed round(s)", w.fails);
    }
    if (w.next_sync_in_ms) out.printf("next    in %" PRIu32 " s", w.next_sync_in_ms / 1000);
    if (w.last_fail[0]) out.printf("last    no time from %s", w.last_fail);
    if (w.state != transport::WifiState::Online) out.line("        (needs Wi-Fi online)");
    return Status::Ok;
}

Status cmd_sntp_sync(Args const&, Sink& out) {
    const auto w = svc::net().wifi();
    if (w.state != transport::WifiState::Online) {
        out.printf("not online (wifi %s) -- nothing to ask", transport::name(w.state));
        return Status::NotReady;
    }
    svc::net().sntp_now();
    out.line("asking the time servers now -- `net sntp` for the result");
    return Status::Ok;
}

// Through `ui`, not straight to `net`: the mode is what lights the row and what the knob
// can cancel, and a window the pixels do not show is a window nobody knows is open.
Status cmd_pair(Args const& a, Sink& out) {
    const auto v = a.sv(0);
    if (v.empty() || v == "on") {
        svc::ui().set_mode(svc::Ui::Mode::Pairing);
        if (!wait_for([] { return svc::net().snapshot().pairing; }, 300)) {
            out.line("refused: the radio is off (rear toggle) or there is no BLE stack");
            return Status::NotReady;
        }
        out.printf("pairing window open for %" PRIu32 " s -- pair from the phone now",
                   svc::net().snapshot().pair_left_ms / 1000);
        return Status::Ok;
    }
    if (v == "off") {
        if (svc::ui().snapshot().mode == svc::Ui::Mode::Pairing) {
            svc::ui().set_mode(svc::Ui::Mode::Idle);
        } else {
            (void)svc::net().pair(false);
        }
        out.line("pairing window closed");
        return Status::Ok;
    }
    out.line("usage: net ble pair [on|off]");
    return Status::BadArg;
}

Status cmd_unbond(Args const&, Sink& out) {
    out.printf("forgetting %u phone(s); a connected one is dropped",
               svc::net().snapshot().link.bonds);
    svc::net().unbond();
    return Status::Ok;
}

Status set_u32(Args const& a, Sink& out, const char* what, uint32_t now, uint32_t lo, uint32_t hi,
               void (Net::*set)(uint32_t) noexcept, uint32_t scale) {
    if (a.count() == 0) {
        out.printf("%s %" PRIu32, what, now / scale);
        return Status::Ok;
    }
    char* end = nullptr;
    const unsigned long v = std::strtoul(a.arg(0), &end, 10);
    if (!end || *end || v < lo || v > hi) {
        out.printf("%s: want %" PRIu32 "..%" PRIu32, what, lo, hi);
        return Status::BadArg;
    }
    (svc::net().*set)(static_cast<uint32_t>(v) * scale);
    out.printf("%s %lu", what, v);
    return Status::Ok;
}

Status cmd_window(Args const& a, Sink& out) {
    return set_u32(a, out, "window_s", svc::net().snapshot().window_ms, 1, 600, &Net::set_window_ms,
                   1000);
}

Status cmd_period(Args const& a, Sink& out) {
    return set_u32(a, out, "period_ms", svc::net().snapshot().period_ms, 100, 3'600'000,
                   &Net::set_period_ms, 1);
}

constexpr CmdSpec kNet[] = {
    {"net", nullptr, "status", "", "radio, BLE link, pairing window", ReleaseOk, cmd_status},
    {"net", "ble", "status", "", "same as `net status`", ReleaseOk, cmd_status},
    {"net", "ble", "pair", "[on|off]", "open the pairing window (= hold the knob 10 s)", ReleaseOk,
     cmd_pair},
    {"net", "ble", "unbond", "", "forget every bonded phone", ReleaseOk, cmd_unbond},
    {"net", "ble", "window", "[<s>]", "how long a pairing window stays open", None, cmd_window},
    {"net", "ble", "period", "[<ms>]", "status snapshot cadence (notify + `sys snap`)", None,
     cmd_period},
    {"net", "wifi", "", "", "Wi-Fi: network, state, last failure", ReleaseOk, cmd_wifi},
    {"net", "wifi", "status", "", "same as `net wifi`", ReleaseOk, cmd_wifi},
    {"net", "wifi", "join", "<ssid> [<psk>]", "store a network and join it (hex:<bytes> ok)",
     ReleaseOk, cmd_wifi_join},
    {"net", "wifi", "forget", "", "forget the stored network, disconnect", ReleaseOk,
     cmd_wifi_forget},
    {"net", "wifi", "scan", "", "list networks in range (~3 s)", ReleaseOk, cmd_wifi_scan},
    {"net", "sntp", "", "", "time servers, last sync, next", ReleaseOk, cmd_sntp},
    {"net", "sntp", "sync", "", "ask the time servers now", ReleaseOk, cmd_sntp_sync},
};

// ---- sys snap -----------------------------------------------------------------------------

constexpr const char* kMotion[] = {"uninit", "homing", "idle", "moving", "fault"};
constexpr const char* kMode[] = {"idle",   "bell",    "alarm",   "clock",
                                 "volume", "pairing", "ringing", "snoozed"};
constexpr const char* kPx[] = {"dial0", "dial1", "bell", "alarm", "clock", "vol", "batt"};

template <std::size_t N>
const char* pick(const char* const (&t)[N], unsigned i) {
    return i < N ? t[i] : "?";
}

}  // namespace

// Declared in cmd_sys.cpp's table.  Here because it is the same record `net` serves.
Status cmd_sys_snap(Args const& a, Sink& out) {
    using namespace transport;
    const Snapshot s = svc::net().status();
    const uint32_t f = s.flags;
    auto on = [f](uint32_t bit) { return (f & bit) != 0; };

    if (a.has_flag("--hex")) {
        uint8_t w[kWireSize];
        const std::size_t n = encode(s, w, sizeof w);
        for (std::size_t i = 0; i < n; i += 32) {
            char line[3 * 32 + 8];
            int k = std::snprintf(line, sizeof line, "%03zu ", i);
            for (std::size_t j = i; j < n && j < i + 32; ++j)
                k += std::snprintf(line + k, sizeof line - static_cast<std::size_t>(k), "%02x",
                                   w[j]);
            out.line(line);
        }
        return Status::Ok;
    }

    out.printf("snap #%u   up %" PRIu32 " s   fw %08" PRIx32
               "   schema %u, %zu B   flags %08" PRIx32,
               s.seq, s.uptime_s, s.fw_id, kSchema, kWireSize, f);
    if (on(kTimeValid)) {
        // Local = UTC + offset (app/PROTOCOL.md §5); without a date only the time of day is real.
        const std::time_t t =
            static_cast<std::time_t>((s.epoch_ms + s.tz_off_min * 60'000LL) / 1000);
        std::tm tm{};
        gmtime_r(&t, &tm);
        char buf[32];
        std::strftime(buf, sizeof buf, on(kDateValid) ? "%Y-%m-%d %H:%M:%S" : "%H:%M:%S (no date)",
                      &tm);
        out.printf("time    %s local   UTC%+d min%s%s   clk %s", buf, s.tz_off_min,
                   on(kTzSet) ? "" : " (unset)", on(kTimeFollow) ? "   hands follow" : "",
                   hal::clock_::name(static_cast<hal::clock_::SlowSrc>(s.clk_src)));
    } else {
        out.printf("time    not set   clk %s",
                   hal::clock_::name(static_cast<hal::clock_::SlowSrc>(s.clk_src)));
    }
    if (on(kPowerOk)) {
        char soc[8];
        if (s.soc_pct == 0xFF)
            std::snprintf(soc, sizeof soc, "?");
        else
            std::snprintf(soc, sizeof soc, "%u%%", s.soc_pct);
        out.printf("power   %u mV (%s)  soc %s%s%s%s%s%s", s.vbat_mv,
                   hal::power::name(static_cast<hal::power::VbatSrc>(s.vbat_src)), soc,
                   on(kPlugged) ? "  plugged" : "  battery", on(kCharging) ? "  charging" : "",
                   on(kChargeFault) ? "  FAULT" : "", on(kFullCharge) ? "  fullchg" : "",
                   on(kBattLow) ? "  LOW" : "");
    } else {
        out.line("power   not present");
    }
    if (on(kEnvOk)) {
        out.printf("room    %.2f C  %.2f %%RH  %.1f hPa  gas %" PRIu32 " ohm%s%s   age %u s",
                   s.temp_cdeg / 100.0, s.rh_cpct / 100.0, s.press_dhpa / 10.0, s.gas_ohms,
                   on(kEnvGasValid) ? "" : " (invalid)", on(kEnvHeatStable) ? "" : " (heating)",
                   s.env_age_s);
    } else {
        out.line("room    not present");
    }
    if (on(kAlsOk)) {
        out.printf("light   %.1f lux%s   age %u s", static_cast<double>(s.lux),
                   on(kAlsSaturated) ? " (saturated)" : "", s.als_age_s);
    } else {
        out.line("light   not present");
    }
    if (on(kImuOk)) {
        out.printf("imu     g %.3f %.3f %.3f m/s2   ypr %.1f %.1f %.1f   taps %u%s",
                   s.grav_mm[0] / 1000.0, s.grav_mm[1] / 1000.0, s.grav_mm[2] / 1000.0,
                   s.ypr_cdeg[0] / 100.0, s.ypr_cdeg[1] / 100.0, s.ypr_cdeg[2] / 100.0, s.taps,
                   on(kImuLink) ? "" : "   (hub not ready)");
    } else {
        out.line("imu     not present");
    }
    out.printf("hands   %s%s%s  %02u:%02u -> %02u:%02u  dial %u  opto %.3f  faults %" PRIu32
               "  trims %u (last %d)",
               pick(kMotion, s.motion_state), on(kHomed) ? "  homed" : "  NOT homed",
               on(kMotorPowered) ? "  powered" : "", s.hand_h, s.hand_m, s.target_h, s.target_m,
               s.dial_tick, s.opto / 65535.0, s.motion_faults, s.trims, s.last_trim);
    out.printf("ui      %s  vol %u%%  alarm %02u:%02u %s  bright %u%%  knob %" PRId32 "%s%s",
               pick(kMode, s.ui_mode), s.volume, s.alarm_h, s.alarm_m,
               on(kAlarmArmed) ? "armed" : "off", s.brightness, s.knob_count,
               on(kKnobPressed) ? " pressed" : "", on(kKnobInput) ? "" : "  INPUT OFF");
    {
        constexpr const char* kNext[] = {"none", "scheduled", "one-off"};
        constexpr const char* kDay[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
        char wk[96];
        int n = 0;
        for (int d = 0; d < 7; ++d)
            n += std::snprintf(wk + n, sizeof wk - static_cast<std::size_t>(n), "%s %s%02u:%02u ",
                               kDay[d], (s.alarm_days >> d) & 1u ? "" : "-", s.alarm_week[d] / 60u,
                               s.alarm_week[d] % 60u);
        out.printf("        next %s%s%s  week %s", pick(kNext, s.alarm_next),
                   s.alarm_next_wday < 7 ? " " : "",
                   s.alarm_next_wday < 7 ? kDay[s.alarm_next_wday] : "", wk);
    }
    char px[160];
    int k = 0;
    for (std::size_t i = 0; i < kPixels; ++i) {
        k += std::snprintf(px + k, sizeof px - static_cast<std::size_t>(k), "%s %02x%02x%02x%02x ",
                           kPx[i], s.px[i][0], s.px[i][1], s.px[i][2], s.px[i][3]);
    }
    out.printf("leds    %s", px);
    out.printf("        wake %u%% warm %u%% cool   amp %s%s", s.wake_warm, s.wake_cool,
               on(kAmpActive) ? "on" : "off", on(kAudioPlaying) ? "  playing" : "");
    out.printf("radio   %s   ble %s   bonds %u   wifi %s%s%s%s", on(kRadioOff) ? "OFF" : "on",
               name(static_cast<BleState>(s.ble_state)), s.bonds,
               name(static_cast<WifiState>(s.wifi_state)), s.wifi_err ? " (" : "",
               s.wifi_err ? name(static_cast<WifiErr>(s.wifi_err)) : "", s.wifi_err ? ")" : "");
    if (s.wifi_rssi)
        out.printf("        wifi %d dBm%s%s", s.wifi_rssi, on(kNetSynced) ? "   sntp synced" : "",
                   on(kNetLocked) ? "   net owns time" : "");
    out.printf("chip    heap %" PRIu32 " free, %" PRIu32 " low-water   reset reason %u",
               s.heap_free, s.heap_min, s.reset_reason);
    return Status::Ok;
}

extern const CmdTable kTableNet{kNet, sizeof(kNet) / sizeof(kNet[0])};

void bind_net() noexcept {
    const auto& b = build_info();
    char info[200];
    std::snprintf(
        info, sizeof info, "fw=%s sha=%s built=%s board=%s profile=%s sdk=%s proto=1 schema=%u",
        b.app_version, b.git_sha, b.build_utc, b.board, b.profile, b.sdk, transport::kSchema);
    auto& n = svc::net();
    n.set_dispatch(&dispatch_line_wait);
    n.set_identity("clock", info, transport::fw_id(b.git_sha));
    svc::storage().set_fw_id(transport::fw_id(b.git_sha));  // stamped in history file headers
}

}  // namespace clk::cli
