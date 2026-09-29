// The history log: the 24-byte record, the day files, the budget, and the download.
// [FIRMWARE.md §6.3a, app/PROTOCOL.md "History"]
//
// The record is a wire format the phone decodes, so it is pinned byte for byte against the
// golden vector in app/protocol.json -- as the status snapshot is.  The AO half runs against
// the fake card and the fake phone: records appear on the UTC grid, land in day files, a
// download over `bulk` reproduces the file exactly, and retention removes by age and by size.
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "check.hpp"
#include "testutil.hpp"

#include "clk/hal/host/sim.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/storage.hpp"
#include "clk/transport/history.hpp"

using namespace clk;
namespace sim = hal::host;
namespace hist = clk::transport::hist;

namespace {

template <class Fn>
bool until(Fn pred, int max_ms = 3000) {
    for (int i = 0; i < max_ms / 5; ++i) {
        if (pred()) return true;
        hal::clock_::sleep_ms(5);
    }
    return pred();
}

std::string hex(const uint8_t* p, std::size_t n) {
    std::string s;
    char b[3];
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, "%02x", p[i]);
        s += b;
    }
    return s;
}

// The string value of "<key>": "..." after `after` in the contract file.
std::string json_str(std::string const& json, const char* after, const char* key) {
    const auto a = json.find(after);
    if (a == std::string::npos) return {};
    const auto k = json.find(std::string("\"") + key + "\"", a);
    if (k == std::string::npos) return {};
    const auto q0 = json.find('"', json.find(':', k) + 1);
    const auto q1 = json.find('"', q0 + 1);
    return json.substr(q0 + 1, q1 - q0 - 1);
}

hist::Sample golden_sample() {
    hist::Sample s;
    s.t = 1790577000;  // 2026-09-28T06:30:00Z
    s.n = 30;
    s.temp_cdeg = 2153;
    s.rh_cpct = 4012;
    s.press_dhpa = 10132;
    s.gas_ohms = 51234;
    s.lux = 250.5f;
    s.lux_max = 1200.0f;
    s.vbat_mv = 4012;
    s.soc_pct = 87;
    s.rssi = -54;
    s.flags = hist::kEnvOk | hist::kGasValid | hist::kHeatStable | hist::kAlsOk | hist::kPowerOk |
              hist::kPlugged | hist::kWifiOnline | hist::kAlarmArmed | hist::kTimeSntp;
    return s;
}

}  // namespace

void test_history_record_round_trips() {
    uint8_t o[hist::kRecord];
    hist::encode(golden_sample(), o);
    hist::Record r;
    CHECK(hist::decode(o, r));
    CHECK(r.kind == hist::kSample);
    CHECK(r.s.t == 1790577000 && r.s.n == 30 && r.s.temp_cdeg == 2153 && r.s.rh_cpct == 4012);
    CHECK(r.s.press_dhpa == 10132 && r.s.vbat_mv == 4012 && r.s.soc_pct == 87 && r.s.rssi == -54);
    CHECK(std::abs(static_cast<long>(r.s.gas_ohms) - 51234) < 20);  // 0.03 % steps
    CHECK(std::fabs(r.s.lux - 250.5f) < 0.2f);
    CHECK(std::fabs(r.s.lux_max - 1200.0f) / 1200.0f < 0.06f);  // the peak is coarse
    CHECK(r.s.flags == golden_sample().flags);

    // A flipped bit is caught; the record after it is still aligned and fine.
    o[7] ^= 0x10;
    CHECK(!hist::decode(o, r));

    // Negative temperatures, saturation, the extremes of the encodings.
    hist::Sample c;
    c.temp_cdeg = -1234;
    c.gas_ohms = 0;
    c.lux = 0.0f;
    hist::encode(c, o);
    CHECK(hist::decode(o, r) && r.s.temp_cdeg == -1234 && r.s.lux == 0.0f);
    CHECK(hist::enc_lux(88000.0f) < 65535 && hist::enc_lux(-1.0f) == 0);
    CHECK(hist::dec_gas(hist::enc_gas(10'000'000)) > 9'990'000);

    hist::Event e;
    e.t = 1790577123;
    e.code = hist::Ev::AlarmFire;
    e.args[0] = 6;
    e.args[1] = 45;
    hist::encode(e, o);
    CHECK(hist::decode(o, r) && r.kind == hist::kEvent && r.e.code == hist::Ev::AlarmFire);
    CHECK(r.e.t == 1790577123 && r.e.args[0] == 6 && r.e.args[1] == 45);
    CHECK_STREQ(hist::name(hist::Ev::SntpSync), "sntp-sync");
}

// app/protocol.json -> history.golden: the iOS decoder is tested against the same bytes.
void test_history_matches_the_app_contract() {
    std::FILE* f = std::fopen(CLK_PROTOCOL_JSON, "rb");
    CHECK(f != nullptr);
    if (!f) return;
    std::string json;
    char buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) json.append(buf, n);
    std::fclose(f);
    uint8_t o[hist::kRecord];
    hist::encode(golden_sample(), o);
    CHECK(json_str(json, "\"history\"", "sample_hex") == hex(o, sizeof o));
    hist::Event e;
    e.t = 1790577123;
    e.code = hist::Ev::AlarmFire;
    e.args[0] = 6;
    e.args[1] = 45;
    hist::encode(e, o);
    CHECK(json_str(json, "\"history\"", "event_hex") == hex(o, sizeof o));
    hist::Header h{300, 1790553600, 0x0ed6214f};
    uint8_t hb[hist::kHeader];
    hist::encode(h, hb);
    CHECK(json_str(json, "\"history\"", "header_hex") == hex(hb, sizeof hb));
}

void test_history_files_and_budget() {
    hist::Header h{300, 1790553600, 0x0ed6214f}, back{};
    uint8_t hb[hist::kHeader];
    hist::encode(h, hb);
    CHECK(hist::decode_header(hb, sizeof hb, back));
    CHECK(back.period_s == 300 && back.day == 1790553600 && back.fw_id == 0x0ed6214f);
    hb[0] = 'X';
    CHECK(!hist::decode_header(hb, sizeof hb, back));

    char p[40];
    CHECK(hist::day_path(1790577000, p, sizeof p));
    CHECK_STREQ(p, "/sd/log/2026/0928.bin");
    CHECK(hist::yyyymmdd(1790577000) == 20260928);
    CHECK(hist::day_start(1790577000) == 1790553600);
    CHECK(hist::from_yyyymmdd(20260928) == 1790553600);
    CHECK(hist::from_yyyymmdd(20260231) == 0);
    CHECK(hist::from_yyyymmdd(20280229) != 0);
    CHECK(hist::from_yyyymmdd(20270229) == 0);

    // The numbers in FIRMWARE.md §6.3a: 5 min x 2 years is one 32 KB cluster a day = 24 MB on
    // a 32 GB card (5.9 MB of data); 10 s still fits 200 MB; 5 s would not (and is refused by
    // the range anyway).
    CHECK(hist::day_bytes(300, 32768) == 32768);
    CHECK(hist::projected_bytes(300, 731, 32768) == 23'953'408ull);
    CHECK(hist::projected_bytes(300, 731, 0) == 5'953'264ull);
    CHECK(hist::projected_bytes(10, 731, 32768) == 167'673'856ull);
    CHECK(hist::projected_bytes(5, 731, 32768) > 200'000'000ull);
}

void run_history_tests() {
    test_history_record_round_trips();
    test_history_matches_the_app_contract();
    test_history_files_and_budget();
}

// ---- the AO, with every service running --------------------------------------------------

namespace {
std::string g_card;

std::vector<uint8_t> read_file(std::string const& path) {
    std::vector<uint8_t> v;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return v;
    uint8_t b[4096];
    for (std::size_t n; (n = std::fread(b, 1, sizeof b, f)) > 0;) v.insert(v.end(), b, b + n);
    std::fclose(f);
    return v;
}

void write_file(std::string const& path, std::size_t bytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::vector<uint8_t> z(64 * 1024, 0);
    for (std::size_t left = bytes; left;) {
        const std::size_t n = left < z.size() ? left : z.size();
        std::fwrite(z.data(), 1, n, f);
        left -= n;
    }
    std::fclose(f);
}

bool exists(std::string const& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0;
}
}  // namespace

// Settings are range-checked and budget-checked.
void test_log_settings_are_budgeted() {
    RecordingSink r;
    CHECK(run("log status", r) == Status::Ok);
    CHECK(r.contains("period=300"));  // the default: five minutes
    CHECK(r.contains("keep=731"));
    CHECK(r.contains("cap=200"));
    RecordingSink bad;
    CHECK(run("log period 5", bad) == Status::BadArg);
    CHECK(run("log keep 3650", bad) == Status::Ok);  // 5 min x 10 years = 120 MB: fits
    RecordingSink deny;
    CHECK(run("log period 10", deny) == Status::Denied);  // 10 s x 10 years does not
    CHECK(deny.contains("> cap 200 MB"));
    CHECK(run("log keep 731", r) == Status::Ok);
    CHECK(run("log period 10", r) == Status::Ok);  // 168 MB: fits
}

// Records on the 10 s grid, then on the card in the day's file, then over `bulk` byte for byte.
void test_log_records_and_downloads() {
    char tmpl[] = "/tmp/clk-log-XXXXXX";
    const char* d = ::mkdtemp(tmpl);
    CHECK(d != nullptr);
    if (!d) return;
    g_card = d;
    sim::set_sd_dir(d);
    RecordingSink r;
    run("storage sd mount", r);
    CHECK(run("chrono time epoch 1790577000000 0", r) == Status::Ok);  // 06:30:00Z
    sim::set_env(21.5f, 40.0f, 1013.2f, 50000);
    sim::set_lux(300.0f);
    // The ring already holds what the earlier tests' clocks produced (other dates): wait for
    // four NEW records.
    // Step sim time a period at a time rather than warping: deterministic on a loaded machine.
    const uint32_t ram0 = svc::storage().log_snapshot().ram;
    for (int i = 0; i < 8; ++i) {
        const uint32_t before = svc::storage().log_snapshot().ram;
        sim::advance(10'000'000);
        (void)until([&] { return svc::storage().log_snapshot().ram > before; }, 1500);
    }
    CHECK(svc::storage().log_snapshot().ram >= ram0 + 6);

    RecordingSink tail;
    CHECK(run("log tail 20", tail) == Status::Ok);
    CHECK(tail.contains("event log-config"));  // the settings change above, as an event
    CHECK(tail.contains("n="));
    CHECK(tail.contains("2026-09-28 06:3"));

    RecordingSink days;
    CHECK(run("log days", days) == Status::Ok);  // flushes first
    CHECK(days.contains("day=20260928/"));
    const std::string path = g_card + "/log/2026/0928.bin";
    const auto file = read_file(path);
    CHECK((file.size() - hist::kHeader) % hist::kRecord == 0);
    hist::Header h{};
    CHECK(hist::decode_header(file.data(), file.size(), h));
    CHECK(h.period_s == 10 && h.day == 1790553600);
    std::size_t samples = 0;
    uint32_t last_t = 0;
    bool grid = true, room = false;
    for (std::size_t off = hist::kHeader; off + hist::kRecord <= file.size();
         off += hist::kRecord) {
        hist::Record rec;
        CHECK(hist::decode(file.data() + off, rec));
        if (rec.kind != hist::kSample) continue;
        // Earlier tests' records for the same day may be in the file too, and another AO may
        // re-set the clock meanwhile: judge the records from our time on.
        if (rec.s.t < 1790577000) continue;
        ++samples;
        if (rec.s.t % 10 != 0 || rec.s.t == last_t) grid = false;
        last_t = rec.s.t;
        if ((rec.s.flags & hist::kEnvOk) && std::abs(rec.s.temp_cdeg - 2150) < 20) room = true;
    }
    CHECK(samples >= 3);
    CHECK(grid);  // on the 10 s UTC grid, one record per period
    CHECK(room);  // the room went in as a scene and came out of the record

    // The download: a bonded phone listening on `bulk`.
    sim::ble_disconnect();
    sim::ble_reconnect();
    sim::ble_set_mtu(185);
    sim::ble_subscribe(true, true);
    sim::ble_subscribe_bulk(true);
    RecordingSink fr;
    CHECK(run("log fetch 20260928", fr) == Status::Ok);
    const auto now_file = read_file(path);  // the fetch flushed again
    char want[32];
    std::snprintf(want, sizeof want, "size=%zu", now_file.size());
    CHECK(fr.contains(want));
    std::snprintf(want, sizeof want, "crc=%08x",
                  svc::Storage::crc32(0, now_file.data(), now_file.size()));
    CHECK(fr.contains(want));
    std::vector<uint8_t> got(now_file.size(), 0xEE);
    std::size_t received = 0;
    bool in_mtu = true;
    CHECK(until(
        [&] {
            uint8_t pkt[600];
            for (std::size_t n; (n = sim::ble_pop_bulk(pkt, sizeof pkt)) > 0;) {
                if (n > 185 - 3) in_mtu = false;
                const uint32_t off = pkt[0] | pkt[1] << 8 | pkt[2] << 16 | pkt[3] << 24;
                if (off + (n - 4) <= got.size()) std::memcpy(got.data() + off, pkt + 4, n - 4);
                received += n - 4;
            }
            return received >= got.size();
        },
        5000));
    CHECK(in_mtu);
    CHECK(received == now_file.size());
    CHECK(got == now_file);
    CHECK(until([] { return !svc::storage().log_snapshot().fetching; }));

    // Resuming from an offset sends only the rest; at the end, nothing.
    RecordingSink rs;
    std::snprintf(want, sizeof want, "log fetch 20260928 %zu", now_file.size() - 24);
    CHECK(run(want, rs) == Status::Ok);
    CHECK(rs.contains("from="));
    received = 0;
    CHECK(until([&] {
        uint8_t pkt[600];
        for (std::size_t n; (n = sim::ble_pop_bulk(pkt, sizeof pkt)) > 0;) received += n - 4;
        return !svc::storage().log_snapshot().fetching;
    }));
    CHECK(received >= 24);
    RecordingSink none;
    CHECK(run("log fetch 20200101", none) == Status::Failed);
    CHECK(run("log fetch 20260231", none) == Status::BadArg);
    sim::ble_subscribe_bulk(false);
    sim::ble_disconnect();
}

// Retention: a day past `keep` goes at the next write, and so does the oldest day when the
// files together pass the cap -- never today.
void test_log_retention() {
    ::mkdir((g_card + "/log/2020").c_str(), 0755);
    write_file(g_card + "/log/2020/0101.bin", 1000);        // six years old
    write_file(g_card + "/log/2026/0901.bin", 11'000'000);  // in range, but 11 MB
    RecordingSink k;
    CHECK(run("log cap 10", k) == Status::Denied);  // 10 s x 731 days would not fit in 10 MB
    CHECK(run("log period 300", k) == Status::Ok);
    CHECK(run("log keep 300", k) == Status::Ok);  // 300 x one 32 KB cluster = 9.8 MB
    CHECK(run("log cap 10", k) == Status::Ok);
    for (int i = 0; i < 40 && svc::storage().log_snapshot().ram == 0; ++i) {
        sim::advance(300'000'000);
        (void)until([] { return svc::storage().log_snapshot().ram > 0; }, 1500);
    }
    CHECK(svc::storage().log_snapshot().ram >= 1);
    RecordingSink f;
    CHECK(run("log flush", f) == Status::Ok);
    CHECK(until([] { return !exists(g_card + "/log/2020/0101.bin"); }));
    CHECK(until([] { return !exists(g_card + "/log/2026/0901.bin"); }));
    CHECK(exists(g_card + "/log/2026/0928.bin"));  // today stays
    RecordingSink st;
    run("log status", st);
    CHECK(!st.contains("days=0"));
    CHECK(svc::storage().log_snapshot().used_bytes < 10'000'000ull);
    run("log cap 200", st);
    run("log keep 731", st);
}

void run_history_service_tests() {
    test_log_settings_are_budgeted();
    test_log_records_and_downloads();
    test_log_retention();
    RecordingSink r;
    run("log period 300", r);
    run("storage sd unmount", r);
    sim::set_sd_dir("");
}
