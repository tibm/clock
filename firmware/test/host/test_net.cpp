// The app link: framing, the status record, and the `net` AO against the fake BLE stack.
// [FIRMWARE.md §8, §11.1]
//
// The wire formats are tested byte for byte -- an app will be written against these offsets,
// and a silent reorder is the one bug no amount of on-device testing notices.  The AO tests
// drive a fake phone (hal::host::ble_*) through the real policy: who may bond, who may run a
// command, what the knob and the window do to each other.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "check.hpp"
#include "testutil.hpp"

#include "clk/cli/registry.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/net.hpp"
#include "clk/services/ui.hpp"
#include "clk/transport/frame.hpp"
#include "clk/transport/snapshot.hpp"

using namespace clk;
namespace sim = hal::host;
namespace tp = clk::transport;

namespace {

template <class Fn>
bool until(Fn pred, int max_ms = 3000) {
    for (int i = 0; i < max_ms / 5; ++i) {
        if (pred()) return true;
        hal::clock_::sleep_ms(5);
    }
    return pred();
}

bool parse(const char* s, tp::Request& r) {
    return tp::parse_request(reinterpret_cast<const uint8_t*>(s), std::strlen(s), r);
}

struct Frames {
    std::vector<std::string> v;
    static bool emit(void* ctx, const uint8_t* d, std::size_t n) {
        static_cast<Frames*>(ctx)->v.emplace_back(reinterpret_cast<const char*>(d), n);
        return true;
    }
};

// Everything the phone heard for request `id`, reassembled into whole records ("|text",
// "=k=v", "$status" -- the id stripped).  Waits for the terminal `$`.
std::vector<std::string> answer(uint16_t id, int max_ms = 3000) {
    std::vector<std::string> out;
    std::string partial;
    const std::string head = std::to_string(id);
    bool done = false;
    until(
        [&] {
            char f[600];
            while (sim::ble_pop_rsp(f, sizeof f)) {
                std::string s{f};
                if (s.compare(0, head.size(), head) != 0) continue;  // someone else's answer
                const char kind = s[head.size()];
                const std::string text = s.substr(head.size() + 1);
                if (kind == '+') {
                    partial += text;
                    continue;
                }
                out.push_back(std::string(1, kind) + partial + text);
                partial.clear();
                if (kind == '$') done = true;
            }
            return done;
        },
        max_ms);
    return out;
}

bool has(std::vector<std::string> const& v, const char* needle) {
    for (auto const& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

bool in_mode(const char* want, int ms = 2000) {
    return until([&] { return std::strcmp(svc::ui().snapshot().mode_name, want) == 0; }, ms);
}

void drain() {
    char f[600];
    while (sim::ble_pop_rsp(f, sizeof f)) {
    }
}

// A phone that is bonded, connected and listening -- the state every command test wants.
void bonded_phone() {
    sim::ble_disconnect();
    if (svc::net().snapshot().link.bonds == 0) {
        RecordingSink r;
        run("net ble pair", r);
        sim::ble_connect();
        CHECK(sim::ble_pair());
        CHECK(in_mode("idle"));
    } else {
        sim::ble_reconnect();
    }
    sim::ble_set_mtu(247);
    sim::ble_subscribe(true, true);
    drain();
}

}  // namespace

// ---- framing -------------------------------------------------------------------------------

void test_frame_parses_requests() {
    tp::Request r;
    CHECK(parse("7 sys ver", r));
    CHECK(r.id == 7);
    CHECK_STREQ(r.line, "sys ver");

    CHECK(parse("sys ver\r\n", r));  // no id: 0, and a terminal's line ending is not the command
    CHECK(r.id == 0);
    CHECK_STREQ(r.line, "sys ver");

    CHECK(parse("42", r));  // digits with nothing after them are a command, not an id
    CHECK(r.id == 0);
    CHECK_STREQ(r.line, "42");

    CHECK(parse("65535   help", r));
    CHECK(r.id == 65535);
    CHECK_STREQ(r.line, "help");

    CHECK(!parse("65536 help", r));
    CHECK(!parse("", r));
    CHECK(!parse("\r\n", r));
    CHECK(!parse("9 ", r));
    const std::string big(tp::kMaxLine + 1, 'x');
    CHECK(!parse(big.c_str(), r));
    const std::string fits(tp::kMaxLine, 'x');
    CHECK(parse(fits.c_str(), r));
    const uint8_t nul[] = {'s', 0, 'x'};
    CHECK(!tp::parse_request(nul, sizeof nul, r));
}

void test_frame_splits_long_records() {
    Frames f;
    CHECK(tp::emit_record(7, tp::Kind::Line, "hello", 20, &Frames::emit, &f) == 1);
    CHECK(f.v.size() == 1 && f.v[0] == "7|hello");

    f.v.clear();
    CHECK(tp::emit_record(12, tp::Kind::Done, "ok", 20, &Frames::emit, &f) == 1);
    CHECK(f.v[0] == "12$ok");

    f.v.clear();
    tp::emit_record(3, tp::Kind::Line, "", 20, &Frames::emit, &f);  // an empty line is a line
    CHECK(f.v.size() == 1 && f.v[0] == "3|");

    // 50 bytes through 20-byte frames: "3+" carries 18, so 18 + 18 + 14.
    f.v.clear();
    const std::string text = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN";
    CHECK(tp::emit_record(3, tp::Kind::Kv, text.c_str(), 20, &Frames::emit, &f) == 3);
    CHECK(f.v.size() == 3);
    std::string joined;
    for (std::size_t i = 0; i < f.v.size(); ++i) {
        CHECK(f.v[i].size() <= 20);
        CHECK(f.v[i][1] == (i + 1 < f.v.size() ? '+' : '='));
        joined += f.v[i].substr(2);
    }
    CHECK(joined == text);

    // A frame too small to carry a byte of text cannot make progress, and says so.
    f.v.clear();
    CHECK(tp::emit_record(123, tp::Kind::Line, "x", 4, &Frames::emit, &f) == 0);
}

// ---- the status record ----------------------------------------------------------------------

void test_snapshot_layout_is_fixed() {
    tp::Snapshot s;
    s.seq = 0xBEEF;
    s.uptime_s = 123456;
    s.epoch_ms = 1'790'000'000'123LL;
    s.tz_off_min = -300;
    s.reset_reason = 3;
    s.clk_src = 1;
    s.flags = tp::kTimeValid | tp::kPlugged | tp::kEnvOk;
    s.fw_id = 0x0ed6214f;
    s.heap_free = 200000;
    s.heap_min = 150000;
    s.vbat_mv = 3912;
    s.soc_pct = 81;
    s.vbat_src = 1;
    s.temp_cdeg = -1234;
    s.rh_cpct = 4567;
    s.press_dhpa = 10132;
    s.gas_ohms = 98765;
    s.env_age_s = 42;
    s.lux = 143.25f;
    s.als_age_s = 3;
    s.grav_mm[0] = -12;
    s.grav_mm[1] = 9806;
    s.grav_mm[2] = 100;
    s.ypr_cdeg[0] = -17999;
    s.taps = 7;
    s.motion_state = 2;
    s.hand_h = 10;
    s.hand_m = 8;
    s.opto = 5000;
    s.motion_faults = 0x80000001u;
    s.last_trim = -3;
    s.ui_mode = 5;
    s.px[2][2] = 200;
    s.px[6][3] = 9;
    s.knob_count = -1000;
    s.bonds = 2;
    s.wifi_rssi = -60;

    uint8_t w[tp::kWireSize + 8];
    CHECK(tp::encode(s, w, 10) == 0);  // too small: nothing written, nothing claimed
    CHECK(tp::encode(s, w, sizeof w) == tp::kWireSize);
    CHECK(tp::kWireSize == 132);

    // The offsets FIRMWARE.md §8.3 promises an app.  Little-endian throughout.
    CHECK(w[0] == tp::kSchema && w[1] == tp::kWireSize);
    CHECK(w[2] == 0xEF && w[3] == 0xBE);
    int64_t epoch;
    std::memcpy(&epoch, w + 8, 8);
    CHECK(epoch == s.epoch_ms);
    uint32_t flags;
    std::memcpy(&flags, w + 20, 4);
    CHECK(flags == s.flags);
    CHECK(w[36] == (3912 & 0xFF) && w[37] == (3912 >> 8) && w[38] == 81);
    float lux;
    std::memcpy(&lux, w + 52, 4);
    CHECK(lux == 143.25f);
    CHECK(w[72] == 2 && w[74] == 10 && w[75] == 8);
    CHECK(w[88] == 5);
    CHECK(w[96 + 2 * 4 + 2] == 200 && w[96 + 6 * 4 + 3] == 9);
    int32_t knob;
    std::memcpy(&knob, w + 124, 4);
    CHECK(knob == -1000);
    CHECK(w[128] == 2 && static_cast<int8_t>(w[130]) == -60 && w[131] == 0);

    tp::Snapshot d;
    CHECK(tp::decode(w, tp::kWireSize, d));
    CHECK(d.seq == s.seq && d.uptime_s == s.uptime_s && d.epoch_ms == s.epoch_ms);
    CHECK(d.tz_off_min == -300 && d.temp_cdeg == -1234 && d.ypr_cdeg[0] == -17999);
    CHECK(d.grav_mm[0] == -12 && d.grav_mm[1] == 9806);
    CHECK(d.motion_faults == 0x80000001u && d.last_trim == -3 && d.knob_count == -1000);
    CHECK(d.lux == 143.25f && d.px[2][2] == 200 && d.wifi_rssi == -60);
    CHECK(std::memcmp(&d.px, &s.px, sizeof s.px) == 0);

    // A newer firmware's longer record still decodes; a short one or another schema does not.
    w[1] = static_cast<uint8_t>(tp::kWireSize + 8);
    CHECK(tp::decode(w, sizeof w, d));
    CHECK(!tp::decode(w, tp::kWireSize - 1, d));
    w[0] = 2;
    CHECK(!tp::decode(w, sizeof w, d));
}

// The app contract (app/protocol.json) carries a golden vector the iOS decoder is tested
// against.  This is the other half: the SHIPPING encoder must produce exactly those bytes, so
// a layout change here fails until the contract is updated with it.
void test_snapshot_matches_the_app_contract() {
    std::FILE* f = std::fopen(CLK_PROTOCOL_JSON, "rb");
    CHECK(f != nullptr);
    if (!f) return;
    std::string json;
    char buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) json.append(buf, n);
    std::fclose(f);

    const auto key = json.find("\"golden\"");
    const auto hk = json.find("\"hex\"", key);
    const auto q0 = json.find('"', json.find(':', hk) + 1);
    const auto q1 = json.find('"', q0 + 1);
    CHECK(key != std::string::npos && hk != std::string::npos && q1 != std::string::npos);
    if (q1 == std::string::npos) return;
    const std::string want = json.substr(q0 + 1, q1 - q0 - 1);

    // The same values as test_snapshot_layout_is_fixed, which is where they are explained.
    tp::Snapshot s;
    s.seq = 0xBEEF;
    s.uptime_s = 123456;
    s.epoch_ms = 1'790'000'000'123LL;
    s.tz_off_min = -300;
    s.reset_reason = 3;
    s.clk_src = 1;
    s.flags = tp::kTimeValid | tp::kPlugged | tp::kEnvOk;
    s.fw_id = 0x0ed6214f;
    s.heap_free = 200000;
    s.heap_min = 150000;
    s.vbat_mv = 3912;
    s.soc_pct = 81;
    s.vbat_src = 1;
    s.temp_cdeg = -1234;
    s.rh_cpct = 4567;
    s.press_dhpa = 10132;
    s.gas_ohms = 98765;
    s.env_age_s = 42;
    s.lux = 143.25f;
    s.als_age_s = 3;
    s.grav_mm[0] = -12;
    s.grav_mm[1] = 9806;
    s.grav_mm[2] = 100;
    s.ypr_cdeg[0] = -17999;
    s.taps = 7;
    s.motion_state = 2;
    s.hand_h = 10;
    s.hand_m = 8;
    s.opto = 5000;
    s.motion_faults = 0x80000001u;
    s.last_trim = -3;
    s.ui_mode = 5;
    s.px[2][2] = 200;
    s.px[6][3] = 9;
    s.knob_count = -1000;
    s.bonds = 2;
    s.wifi_rssi = -60;

    uint8_t w[tp::kWireSize];
    CHECK(tp::encode(s, w, sizeof w) == tp::kWireSize);
    std::string got;
    for (uint8_t b : w) {
        char h[3];
        std::snprintf(h, sizeof h, "%02x", b);
        got += h;
    }
    CHECK(got == want);
    if (got != want) std::printf("  encoder: %s\n  contract: %s\n", got.c_str(), want.c_str());
}

void test_fw_id_from_describe() {
    CHECK(tp::fw_id("ed6214f") == 0x0ed6214f);
    CHECK(tp::fw_id("ed6214f-dirty") == 0x0ed6214f);
    CHECK(tp::fw_id("v0.2-3-ged6214f") == 0x0ed6214f);
    CHECK(tp::fw_id("v0.2-3-ged6214f-dirty") == 0x0ed6214f);
    CHECK(tp::fw_id("0123456789abcdef") == 0x01234567);
    CHECK(tp::fw_id("unknown") == 0);
    CHECK(tp::fw_id(nullptr) == 0);
}

void run_net_tests() {
    test_frame_parses_requests();
    test_frame_splits_long_records();
    test_snapshot_layout_is_fixed();
    test_snapshot_matches_the_app_contract();
    test_fw_id_from_describe();
}

// ---- the AO, against a fake phone ---------------------------------------------------------
// Called from run_motion_service_tests() while every AO is running, wired as app_main wires
// them -- `net` included.

void test_net_stack_is_up() {
    CHECK(until([] { return svc::net().snapshot().link.up; }));
    CHECK(svc::net().snapshot().ble == tp::BleState::Idle);
    char info[256];
    CHECK(sim::ble_read_info(info, sizeof info) > 0);
    CHECK(std::strstr(info, "proto=1") != nullptr);
}

// The window is the ONLY way to a bond, and the ONLY way to a command.
void test_net_a_stranger_cannot_pair_or_command() {
    sim::ble_disconnect();
    (void)svc::net().pair(false);
    const uint32_t refused = svc::net().snapshot().link.refused;
    sim::ble_connect();
    CHECK(sim::ble_write("1 sys ver") == Status::Denied);  // not even encrypted yet
    CHECK(!sim::ble_pair());
    CHECK(!svc::net().snapshot().link.connected);  // dropped for trying
    CHECK(until([&] { return svc::net().snapshot().link.refused == refused + 1; }));
    CHECK(sim::ble_write("1 sys ver") == Status::Denied);
}

// Ten seconds of hold opens the window and the blue row; a phone bonding closes both, and
// the row says so in green.  Bonding is the only way pairing ends WITHOUT the five-second
// timeout: the radio holds the mode open for its window.
void test_net_hold_pairs_a_phone() {
    sim::ble_disconnect();
    RecordingSink r;
    run("ui knob pair 300", r);
    run("ui knob timeout 400", r);
    const uint32_t bonds = svc::net().snapshot().link.bonds;

    sim::press(60u * 60 * 1000);
    CHECK(in_mode("pairing"));
    CHECK(until([] { return svc::net().snapshot().pairing; }));
    sim::ble_connect();
    sim::press(0);
    // Well past the five-second rule (400 ms here): the radio is holding it.
    hal::clock_::sleep_ms(900);
    CHECK(std::strcmp(svc::ui().snapshot().mode_name, "pairing") == 0);
    CHECK(svc::net().snapshot().ble == tp::BleState::Pairing ||
          svc::net().snapshot().link.connected);

    CHECK(sim::ble_pair());
    CHECK(in_mode("idle"));
    CHECK(svc::net().snapshot().last_end == svc::Net::PairEnd::Bonded);
    CHECK(!svc::net().snapshot().pairing);
    CHECK(svc::net().snapshot().link.bonds == bonds + 1);
    CHECK(until([] { return hal::pixels::get(2).g > 0; }, 2000));  // the green "it worked"
    CHECK(hal::pixels::get(2).b == 0);

    run("ui knob timeout 5000", r);
    run("ui knob pair 10000", r);
}

void test_net_runs_cli_lines() {
    bonded_phone();
    cli::unsafe_set(false);

    CHECK(sim::ble_write("5 sys ver") == Status::Ok);
    auto a = answer(5);
    CHECK(has(a, "|app     clock"));
    CHECK(!a.empty() && a.back() == "$ok");

    // The SAME dispatcher as the console: same errors, same suggestions, same unsafe gate.
    CHECK(sim::ble_write("6 sys stst") == Status::Ok);
    a = answer(6);
    CHECK(has(a, "did you mean"));
    CHECK(!a.empty() && a.back() == "$bad-arg");

    CHECK(sim::ble_write("7 motion home") == Status::Ok);
    a = answer(7);
    CHECK(!a.empty() && a.back() == "$denied");

    // No id: answers come back as id 0.
    CHECK(sim::ble_write("sys ver") == Status::Ok);
    a = answer(0);
    CHECK(!a.empty() && a.back() == "$ok");

    CHECK(sim::ble_write("   ") == Status::Ok);  // blank: not a command
    a = answer(0);
    CHECK(!a.empty() && a.back() == "$bad-arg");

    // A 23-byte MTU splits every line longer than 20 bytes into fragments -- and `help`
    // reassembles to exactly what the console prints.
    sim::ble_set_mtu(23);
    CHECK(sim::ble_write("8 help sys") == Status::Ok);
    a = answer(8);
    RecordingSink console;
    run("help sys", console);
    CHECK(a.size() == console.lines.size() + 1);
    for (std::size_t i = 0; i < console.lines.size() && i < a.size(); ++i)
        CHECK(a[i] == "|" + console.lines[i]);
    sim::ble_set_mtu(247);
    cli::unsafe_set(true);
}

void test_net_serves_the_status_record() {
    bonded_phone();
    RecordingSink r;
    run("net ble period 100", r);
    const uint32_t n0 = sim::ble_status_notifies();
    CHECK(until([&] { return sim::ble_status_notifies() >= n0 + 3; }));

    uint8_t w[tp::kWireSize];
    CHECK(sim::ble_read_status(w, sizeof w) == tp::kWireSize);
    tp::Snapshot a;
    CHECK(tp::decode(w, sizeof w, a));
    CHECK(a.flags & tp::kBleConnected);
    CHECK(a.flags & tp::kBleSecure);
    CHECK(a.ble_state == static_cast<uint8_t>(tp::BleState::Secure));
    CHECK(a.bonds >= 1);
    CHECK(a.ui_mode == static_cast<uint8_t>(svc::ui().snapshot().mode));

    // Timestamped and sequenced: a later read is a later record.
    tp::Snapshot b;
    CHECK(until([&] {
        sim::ble_read_status(w, sizeof w);
        return tp::decode(w, sizeof w, b) && static_cast<uint16_t>(b.seq - a.seq) >= 2;
    }));
    CHECK(b.uptime_s >= a.uptime_s);

    // What the room says goes in as a scene and comes out through the shipping drivers.
    sim::set_env(21.5f, 40.0f, 1000.0f, 50000);
    sim::set_lux(250.0f);
    CHECK(until(
        [] {
            const auto s = svc::net().status();
            return (s.flags & tp::kAlsOk) && std::fabs(s.lux - 250.0f) < 25.0f;
        },
        12000));  // the ALS is sampled every 5 s, and the first read after a `sim reset` re-inits
    const auto room = svc::net().status();
    CHECK(room.flags & tp::kEnvOk);
    CHECK(room.env_age_s != tp::kAgeNever);

    // `sys snap` is the same record, for a human.
    RecordingSink snap;
    CHECK(run("sys snap", snap) == Status::Ok);
    CHECK(snap.contains("snap #"));
    CHECK(snap.contains("hands"));
    CHECK(snap.contains("ble secure"));
    RecordingSink hex;
    CHECK(run("sys snap --hex", hex) == Status::Ok);
    CHECK(hex.lines.size() == 5);  // 132 bytes, 32 a row

    // A subscriber that goes away stops being notified; the value stays readable.
    sim::ble_subscribe(true, false);
    hal::clock_::sleep_ms(150);
    const uint32_t n1 = sim::ble_status_notifies();
    hal::clock_::sleep_ms(400);
    CHECK(sim::ble_status_notifies() == n1);
    run("net ble period 1000", r);
}

void test_net_cli_opens_and_closes_the_window() {
    sim::ble_disconnect();
    RecordingSink r;
    CHECK(run("net ble pair", r) == Status::Ok);
    CHECK(in_mode("pairing"));
    CHECK(svc::net().snapshot().pairing);
    RecordingSink off;
    CHECK(run("net ble pair off", off) == Status::Ok);
    CHECK(in_mode("idle"));
    CHECK(!svc::net().snapshot().pairing);
    CHECK(svc::net().snapshot().last_end == svc::Net::PairEnd::Cancelled);

    // The knob cancels it too: a press in `pairing` is back to idle, and the window goes with it.
    run("net ble pair", r);
    CHECK(until([] { return svc::net().snapshot().pairing; }));
    sim::press(60u * 60 * 1000);
    hal::clock_::sleep_ms(60);
    sim::press(0);
    CHECK(in_mode("idle"));
    CHECK(!svc::net().snapshot().pairing);

    // And it ends on its own.
    run("net ble window 1", r);
    run("net ble pair", r);
    CHECK(in_mode("pairing"));
    CHECK(in_mode("idle", 3000));
    CHECK(svc::net().snapshot().last_end == svc::Net::PairEnd::Expired);
    run("net ble window 120", r);
}

// The rear toggle is a hard override: the stack goes quiet, a connected phone is dropped,
// and neither the knob nor the CLI can open a window until it clears.
void test_net_radio_off_is_absolute() {
    bonded_phone();
    sim::set_expander_in(hal::expander::Sig::RadioOff, false);
    CHECK(until([] { return !svc::net().snapshot().link.up; }, 3000));
    CHECK(!svc::net().snapshot().link.connected);
    RecordingSink r;
    CHECK(run("net ble pair", r) == Status::NotReady);
    CHECK(r.contains("radio is off"));
    CHECK(in_mode("idle"));

    sim::set_expander_in(hal::expander::Sig::RadioOff, true);
    CHECK(until([] { return svc::net().snapshot().link.up; }, 3000));
    sim::ble_reconnect();
    CHECK(svc::net().snapshot().link.bonded || sim::ble_write("1 sys ver") == Status::Ok);
    sim::ble_disconnect();
}

// The phone's three commands (app/PROTOCOL.md §4): a whole instant + offset, the offset alone,
// and the alarm.  Local = UTC + offset everywhere -- the hands, `chrono`, the snapshot.
void test_net_sets_time_and_alarm() {
    auto& c = svc::chrono();
    RecordingSink r;
    // 2026-09-28 06:30:00 UTC, at UTC+02:00: the hands show 08:30.
    constexpr long long kUtc = 1790577000000LL;
    CHECK(run("chrono time epoch 1790577000000 120", r) == Status::Ok);
    CHECK(r.contains("2026-09-28 08:30"));
    CHECK(until([] {
        const auto s = svc::chrono().snapshot();
        return s.valid && s.hour == 8 && s.minute == 30 && s.date_valid && s.tz_set;
    }));
    CHECK(c.snapshot().tz_off_min == 120);
    const long long drift = c.snapshot().epoch_ms - kUtc;
    CHECK(drift >= 0 && drift < 60'000);  // true UTC, not local-as-UTC

    // The snapshot carries the same three facts, in the contract's form.
    CHECK(until([] {
        const auto s = svc::net().status();
        return (s.flags & tp::kTzSet) && (s.flags & tp::kDateValid) && s.tz_off_min == 120 &&
               s.epoch_ms >= kUtc;
    }));

    // A time of day keeps the date and is LOCAL.
    RecordingSink t;
    CHECK(run("chrono time set 07:15", t) == Status::Ok);
    CHECK(until([] {
        const auto s = svc::chrono().snapshot();
        return s.hour == 7 && s.minute == 15;
    }));
    const auto s1 = c.snapshot();
    CHECK(s1.date_valid);
    CHECK((s1.epoch_ms + 120 * 60'000LL) / 86'400'000LL == (kUtc + 120 * 60'000LL) / 86'400'000LL);

    // The offset alone: same instant, the hands move an hour (a DST change, as the phone sends it).
    RecordingSink z;
    CHECK(run("chrono tz 60", z) == Status::Ok);
    CHECK(until([] { return svc::chrono().snapshot().hour == 6; }));
    CHECK(std::llabs(c.snapshot().epoch_ms - s1.epoch_ms) < 5'000);

    // Refusals: seconds instead of milliseconds, an offset off the map, garbage.
    RecordingSink bad;
    CHECK(run("chrono time epoch 1790577000 120", bad) == Status::BadArg);
    CHECK(bad.contains("looks like seconds"));
    CHECK(run("chrono time epoch 1790577000000 900", bad) == Status::BadArg);
    CHECK(run("chrono tz -800", bad) == Status::BadArg);
    CHECK(run("chrono time epoch soon", bad) == Status::BadArg);

    // The alarm -- and the answer is what `ui` took, not what was asked.
    RecordingSink al;
    CHECK(run("chrono alarm set 06:45", al) == Status::Ok);
    CHECK(al.contains("alarm 06:45"));
    CHECK(run("chrono alarm arm on", al) == Status::Ok);
    CHECK(svc::ui().snapshot().alarm_armed);
    CHECK(svc::ui().snapshot().alarm_hour == 6 && svc::ui().snapshot().alarm_minute == 45);
    CHECK(until([] {
        const auto s = svc::net().status();
        return (s.flags & tp::kAlarmArmed) && s.alarm_h == 6 && s.alarm_m == 45;
    }));
    CHECK(run("chrono alarm set 25:00", bad) == Status::BadArg);
    CHECK(run("chrono alarm arm maybe", bad) == Status::BadArg);
    RecordingSink show;
    CHECK(run("chrono alarm", show) == Status::Ok);
    CHECK(show.contains("06:45 armed"));

    // And the same over the air.
    bonded_phone();
    CHECK(sim::ble_write("11 chrono alarm arm off") == Status::Ok);
    auto a = answer(11);
    CHECK(!a.empty() && a.back() == "$ok");
    CHECK(!svc::ui().snapshot().alarm_armed);
    CHECK(sim::ble_write("12 chrono time epoch 1790577000000 0") == Status::Ok);
    a = answer(12);
    CHECK(!a.empty() && a.back() == "$ok");
    CHECK(until([] { return svc::chrono().snapshot().hour == 6; }));
    sim::ble_disconnect();
    run("chrono tz 0", r);
}

void run_net_service_tests() {
    test_net_stack_is_up();
    test_net_a_stranger_cannot_pair_or_command();
    test_net_hold_pairs_a_phone();
    test_net_runs_cli_lines();
    test_net_serves_the_status_record();
    test_net_cli_opens_and_closes_the_window();
    test_net_radio_off_is_absolute();
    test_net_sets_time_and_alarm();
    sim::ble_disconnect();
}
