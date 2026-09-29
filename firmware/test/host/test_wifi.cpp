// Wi-Fi, SNTP and time zones.                                   [FIRMWARE.md §6.4, §6.7]
//
// The pure halves first -- the POSIX TZ rules against dates checked by hand, the SNTP packet
// checks against every way a server can be wrong -- then the `net` AO against the fake radio
// and a scripted internet: join, fail for a reason, back off, forget; ask three servers in
// order and take the first believable answer.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "check.hpp"
#include "testutil.hpp"

#include "clk/domain/sntp.hpp"
#include "clk/domain/tz.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/net.hpp"
#include "clk/services/ui.hpp"
#include "clk/transport/snapshot.hpp"

using namespace clk;
namespace sim = hal::host;
namespace tz = clk::domain::tz;
namespace sntp = clk::domain::sntp;
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

int64_t utc_s(int y, unsigned mo, unsigned d, int h, int mi) {
    return tz::days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60;
}

}  // namespace

// ---- POSIX TZ ------------------------------------------------------------------------------

void test_tz_parses_and_refuses() {
    tz::Zone z;
    CHECK(tz::parse("PST8PDT,M3.2.0,M11.1.0", z));
    CHECK(z.std_off_s == -8 * 3600 && z.dst_off_s == -7 * 3600 && z.has_dst);
    CHECK(z.start.m == 3 && z.start.w == 2 && z.start.d == 0 && z.start.time_s == 7200);
    CHECK(tz::parse("CET-1CEST,M3.5.0,M10.5.0/3", z));
    CHECK(z.std_off_s == 3600 && z.dst_off_s == 7200 && z.end.time_s == 3 * 3600);
    CHECK(tz::parse("<+0530>-5:30", z));
    CHECK(z.std_off_s == 5 * 3600 + 1800 && !z.has_dst);
    CHECK(tz::parse("UTC0", z) && z.std_off_s == 0);
    CHECK(tz::parse("EST5EDT", z) && z.has_dst && z.start.m == 3);  // glibc's US default
    CHECK(tz::parse("<-03>3", z) && z.std_off_s == -3 * 3600);
    CHECK(tz::parse("IST-2IDT,M3.4.4/26,M10.5.0", z) && z.start.time_s == 26 * 3600);  // Israel
    CHECK(tz::parse("<-01>1<+00>,M3.5.0/0,M10.5.0/1", z));  // Azores-style quoted dst name
    CHECK(tz::parse("XXX3YYY,J60/2,300", z) && z.start.kind == tz::Rule::Julian1);

    // What the iOS app sends (app/clockTests/WifiTests.swift builds these from TimeZone).
    CHECK(tz::parse("<-08>8<-07>,M3.2.0,M11.1.0", z) && z.std_off_s == -8 * 3600 &&
          z.dst_off_s == -7 * 3600);
    CHECK(tz::parse("<+01>-1<+02>,M3.5.0,M10.5.0/3", z) && z.dst_off_s == 7200);
    CHECK(tz::parse("<+10>-10<+11>,M10.1.0,M4.1.0/3", z) && z.start.m == 10);
    CHECK(tz::parse("<+00>0", z) && !z.has_dst);

    CHECK(!tz::parse("", z));
    CHECK(!tz::parse("PST", z));             // no offset
    CHECK(!tz::parse("PS8", z));             // name too short
    CHECK(!tz::parse("PST8PDT,M3.2.0", z));  // half a rule
    CHECK(!tz::parse("PST8PDT,M13.2.0,M11.1.0", z));
    CHECK(!tz::parse("PST8PDT,M3.2.0,M11.1.0junk", z));
    CHECK(!tz::parse("America/Los_Angeles", z));
    CHECK(!tz::parse(nullptr, z));

    char b[24];
    tz::fixed(120, b, sizeof b);
    CHECK_STREQ(b, "<+0200>-2");
    tz::fixed(-210, b, sizeof b);
    CHECK_STREQ(b, "<-0330>3:30");
    tz::fixed(0, b, sizeof b);
    CHECK_STREQ(b, "<+0000>0");
    tz::fixed(345, b, sizeof b);
    CHECK(tz::parse(b, z) && z.std_off_s == 345 * 60);
}

// San Francisco, 2026: PDT from 2026-03-08 10:00Z to 2026-11-01 09:00Z.
void test_tz_san_francisco() {
    tz::Zone z;
    CHECK(tz::parse(tz::kDefault, z));
    CHECK(tz::offset_s(z, utc_s(2026, 1, 15, 12, 0)) == -8 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 3, 8, 9, 59)) == -8 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 3, 8, 10, 0)) == -7 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 9, 28, 12, 0)) == -7 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 11, 1, 8, 59)) == -7 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 11, 1, 9, 0)) == -8 * 3600);
    // 2027: second Sunday of March is the 14th, first Sunday of November the 7th.
    CHECK(tz::offset_s(z, utc_s(2027, 3, 14, 9, 59)) == -8 * 3600);
    CHECK(tz::offset_s(z, utc_s(2027, 3, 14, 10, 0)) == -7 * 3600);
    CHECK(tz::offset_s(z, utc_s(2027, 11, 7, 9, 0)) == -8 * 3600);

    // Local -> UTC around both edges.
    const int64_t l_skip = utc_s(2026, 3, 8, 2, 30);                  // 02:30 does not exist
    CHECK(tz::local_to_utc(z, l_skip) == utc_s(2026, 3, 8, 10, 30));  // = 03:30 PDT
    const int64_t l_dbl = utc_s(2026, 11, 1, 1, 30);                  // 01:30 happens twice
    CHECK(tz::local_to_utc(z, l_dbl) == utc_s(2026, 11, 1, 8, 30));   // the first: PDT
    CHECK(tz::local_to_utc(z, utc_s(2026, 7, 4, 12, 0)) == utc_s(2026, 7, 4, 19, 0));
    CHECK(tz::local_to_utc(z, utc_s(2026, 12, 25, 7, 0)) == utc_s(2026, 12, 25, 15, 0));
}

void test_tz_other_zones() {
    tz::Zone z;
    // Europe: last Sundays, 01:00 UTC both ways.  2026: Mar 29, Oct 25.
    CHECK(tz::parse("CET-1CEST,M3.5.0,M10.5.0/3", z));
    CHECK(tz::offset_s(z, utc_s(2026, 3, 29, 0, 59)) == 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 3, 29, 1, 0)) == 7200);
    CHECK(tz::offset_s(z, utc_s(2026, 10, 25, 0, 59)) == 7200);
    CHECK(tz::offset_s(z, utc_s(2026, 10, 25, 1, 0)) == 3600);
    // Southern hemisphere: summer spans New Year.  Sydney: first Sunday of Oct (2026-10-04,
    // 02:00 AEST = 16:00Z on the 3rd) to first Sunday of April (2026-04-05, 03:00 AEDT =
    // 16:00Z on the 4th).
    CHECK(tz::parse("AEST-10AEDT,M10.1.0,M4.1.0/3", z));
    CHECK(tz::offset_s(z, utc_s(2026, 1, 10, 0, 0)) == 11 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 4, 4, 15, 59)) == 11 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 4, 4, 16, 0)) == 10 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 7, 1, 0, 0)) == 10 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 10, 3, 15, 59)) == 10 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 10, 3, 16, 0)) == 11 * 3600);
    CHECK(tz::offset_s(z, utc_s(2026, 12, 31, 23, 0)) == 11 * 3600);
    // No DST at all.
    CHECK(tz::parse("<+0530>-5:30", z));
    CHECK(tz::offset_s(z, utc_s(2026, 6, 1, 0, 0)) == 19800);
    // Calendar sanity.
    CHECK(tz::weekday(tz::days_from_civil(2026, 9, 28)) == 1);  // a Monday
    CHECK(tz::weekday(tz::days_from_civil(1969, 12, 28)) == 0);
    const auto ymd = tz::civil_from_days(tz::days_from_civil(2028, 2, 29));
    CHECK(ymd.y == 2028 && ymd.m == 2 && ymd.d == 29);
}

// ---- SNTP packets --------------------------------------------------------------------------

namespace {
void put_ts(uint8_t* p, int64_t unix_ms) {
    const uint64_t s = static_cast<uint64_t>(unix_ms / 1000 + sntp::kNtpToUnix);
    const uint64_t ts = (s << 32) | ((static_cast<uint64_t>(unix_ms % 1000) << 32) / 1000);
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(ts >> (56 - 8 * i));
}
void answer(uint8_t (&p)[48], uint8_t (&req)[48], int64_t t_ms, uint8_t stratum = 2,
            unsigned li = 0) {
    std::memset(p, 0, sizeof p);
    p[0] = static_cast<uint8_t>((li << 6) | (4 << 3) | 4);
    p[1] = stratum;
    std::memcpy(p + 24, req + 40, 8);
    put_ts(p + 32, t_ms);
    put_ts(p + 40, t_ms + 1);
}
}  // namespace

void test_sntp_packets() {
    uint8_t req[48];
    sntp::build_request(req, 0x0123456789ABCDEFull);
    CHECK(req[0] == 0x23);  // LI 0, VN 4, client
    CHECK(req[40] == 0x01 && req[47] == 0xEF);

    constexpr int64_t kT = 1'790'577'000'123LL;  // 2026-09-28 06:30:00.123Z
    uint8_t p[48];
    sntp::Reply r{};
    answer(p, req, kT);
    CHECK(sntp::parse(p, 48, 0x0123456789ABCDEFull, r) == sntp::Verdict::Ok);
    CHECK(r.tx_ms == kT + 1 && r.rx_ms == kT);
    CHECK(r.stratum == 2);
    // 40 ms round trip, 1 ms of it inside the server: the answer is 19.5 ms old on arrival.
    CHECK(sntp::utc_at_arrival_ms(r, 1'000'000, 1'040'000) == kT + 1 + 19);

    CHECK(sntp::parse(p, 47, 0x0123456789ABCDEFull, r) == sntp::Verdict::Short);
    CHECK(sntp::parse(p, 48, 0x1111ull, r) == sntp::Verdict::BadOrigin);
    answer(p, req, kT, 0);
    std::memcpy(p + 12, "RATE", 4);
    CHECK(sntp::parse(p, 48, 0x0123456789ABCDEFull, r) == sntp::Verdict::KissOfDeath);
    CHECK_STREQ(r.kod, "RATE");
    answer(p, req, kT, 2, 3);
    CHECK(sntp::parse(p, 48, 0x0123456789ABCDEFull, r) == sntp::Verdict::Unsynced);
    answer(p, req, kT, 16);
    CHECK(sntp::parse(p, 48, 0x0123456789ABCDEFull, r) == sntp::Verdict::Unsynced);
    answer(p, req, kT);
    p[0] = (4 << 3) | 3;  // a client packet echoed back
    CHECK(sntp::parse(p, 48, 0x0123456789ABCDEFull, r) == sntp::Verdict::NotServer);
    answer(p, req, kT);
    std::memset(p + 40, 0, 8);
    CHECK(sntp::parse(p, 48, 0x0123456789ABCDEFull, r) == sntp::Verdict::NoTime);
    answer(p, req, 1'000'000'000'000LL);  // 2001
    CHECK(sntp::parse(p, 48, 0x0123456789ABCDEFull, r) == sntp::Verdict::TooEarly);

    // Era 1: 2036-02-07T06:28:16Z rolls the 32-bit seconds over; 2040 must still read as 2040.
    constexpr int64_t k2040 = 2'208'988'800'000LL;  // 2040-01-01T00:00:00Z
    answer(p, req, k2040);
    CHECK(sntp::parse(p, 48, 0x0123456789ABCDEFull, r) == sntp::Verdict::Ok);
    CHECK(r.rx_ms == k2040);
}

void run_wifi_tests() {
    test_tz_parses_and_refuses();
    test_tz_san_francisco();
    test_tz_other_zones();
    test_sntp_packets();
}

// ---- the AO, against the fake radio + internet ------------------------------------------
// Called from run_net_service_tests() with every AO running.

namespace {
tp::WifiState wstate() { return svc::net().wifi().state; }
}  // namespace

// Nothing stored: Idle, and the knob still owns the clock.
void test_wifi_starts_idle() {
    CHECK(until([] { return wstate() == tp::WifiState::Idle; }));
    const auto w = svc::net().wifi();
    CHECK(!w.provisioned && !w.synced);
    RecordingSink r;
    CHECK(run("net wifi", r) == Status::Ok);
    CHECK(r.contains("none stored"));
    CHECK(r.contains("state=idle"));
}

// The wrong password fails AS a wrong password, backs off, and is retried; the right one
// joins, and the first time server's answer sets the clock -- date, UTC, and the zone's local.
void test_wifi_join_and_sync() {
    sim::wifi_clear();
    sim::wifi_add_ap("home", "correct horse", -48);
    sim::wifi_add_ap("cafe", "", -70);
    // 2026-07-04 19:00:00Z = 12:00 PDT in the default zone.
    constexpr int64_t kUtc = 1'783'191'600'000LL;
    sim::wifi_set_utc(kUtc);
    RecordingSink tzr;
    CHECK(run("chrono tz PST8PDT,M3.2.0,M11.1.0 America/Los_Angeles", tzr) == Status::Ok);

    RecordingSink scan;
    CHECK(run("net wifi scan", scan) == Status::Ok);
    CHECK(scan.contains("ap=-48/secured/6/home"));
    CHECK(scan.contains("ap=-70/open/6/cafe"));

    RecordingSink bad;
    CHECK(run("net wifi join home wrongpass", bad) == Status::Ok);
    CHECK(until([] {
        const auto w = svc::net().wifi();
        return w.state == tp::WifiState::Backoff && w.err == tp::WifiErr::Auth;
    }));
    CHECK(svc::net().wifi().retry_in_ms > 0);
    CHECK(until(
        [] { return (svc::net().status().wifi_err == static_cast<uint8_t>(tp::WifiErr::Auth)); },
        2000));
    RecordingSink st;
    run("net wifi", st);
    CHECK(st.contains("wrong password"));
    CHECK(st.contains("err=auth"));

    // The app's form: hex, so a space in the password costs nothing.
    RecordingSink good;
    CHECK(run("net wifi join hex:686f6d65 hex:636f727265637420686f727365", good) == Status::Ok);
    CHECK(good.contains("saved \"home\""));
    CHECK(until([] { return wstate() == tp::WifiState::Online; }));
    CHECK(until([] { return svc::net().wifi().synced; }));
    const auto w = svc::net().wifi();
    CHECK(w.server == 0);
    CHECK(w.err == tp::WifiErr::None);
    CHECK(sim::wifi_ntp_requests(svc::Net::kSntpServers[0]) >= 1);
    CHECK(sim::wifi_ntp_requests(svc::Net::kSntpServers[1]) == 0);  // never needed

    CHECK(until([] {
        const auto c = svc::chrono().snapshot();
        return c.src == svc::Chrono::Source::Sntp && c.date_valid && c.hour == 12;
    }));
    const auto c = svc::chrono().snapshot();
    CHECK(c.tz_off_min == -420 && c.tz_dst);
    // The fake internet's clock keeps running while the test waits out a backoff: the answer
    // is kUtc plus however long that took, which is seconds, not minutes.
    CHECK(c.epoch_ms >= kUtc && c.epoch_ms - kUtc < 60'000);
    CHECK(c.net_provisioned && c.net_synced);
    CHECK(until([] { return svc::ui().snapshot().net_locked; }, 2000));  // the knob stands down

    CHECK(until(
        [] {
            const auto s = svc::net().status();
            return s.wifi_state == static_cast<uint8_t>(tp::WifiState::Online) &&
                   s.wifi_rssi == -48 && (s.flags & tp::kNetSynced) &&
                   (s.flags & tp::kNetProvisioned);
        },
        3000));
    RecordingSink ns;
    CHECK(run("net sntp", ns) == Status::Ok);
    CHECK(ns.contains("<- last answer"));
}

// The first server silent, the second a kiss-of-death: the third is asked, in that order,
// and its answer is taken.  All three bad: the round fails and is retried later.
void test_sntp_falls_back_in_order() {
    const char* const* s = svc::Net::kSntpServers;
    sim::wifi_ntp(s[0], sim::NtpMode::Silent);
    sim::wifi_ntp(s[1], sim::NtpMode::Kiss);
    const uint32_t n0 = sim::wifi_ntp_requests(s[0]), n1 = sim::wifi_ntp_requests(s[1]),
                   n2 = sim::wifi_ntp_requests(s[2]);
    const uint32_t syncs = svc::net().wifi().syncs;
    hal::host::set_warp(20.0);  // the silent server's 3 s timeout, in 150 ms
    RecordingSink r;
    CHECK(run("net sntp sync", r) == Status::Ok);
    CHECK(until([&] { return svc::net().wifi().syncs == syncs + 1; }, 3000));
    CHECK(svc::net().wifi().server == 2);
    CHECK(sim::wifi_ntp_requests(s[0]) == n0 + 1);
    CHECK(sim::wifi_ntp_requests(s[1]) == n1 + 1);
    CHECK(sim::wifi_ntp_requests(s[2]) == n2 + 1);
    CHECK(std::strstr(svc::net().wifi().last_fail, "kiss-of-death") != nullptr);

    sim::wifi_ntp(s[2], sim::NtpMode::NoDns);
    const uint32_t fails = svc::net().wifi().fails;
    CHECK(run("net sntp sync", r) == Status::Ok);
    CHECK(until([&] { return svc::net().wifi().fails == fails + 1; }, 3000));
    CHECK(svc::net().wifi().next_sync_in_ms > 0);
    CHECK(svc::net().wifi().synced);  // still synced from before: one bad round loses nothing
    hal::host::set_warp(1.0);
    sim::wifi_clear();
    sim::wifi_add_ap("home", "correct horse", -48);
}

// An unsynchronised server is not believed either; the next one is.
void test_sntp_refuses_unsynced() {
    const char* const* s = svc::Net::kSntpServers;
    sim::wifi_ntp(s[0], sim::NtpMode::Unsynced);
    const uint32_t syncs = svc::net().wifi().syncs;
    RecordingSink r;
    run("net sntp sync", r);
    CHECK(until([&] { return svc::net().wifi().syncs == syncs + 1; }, 3000));
    CHECK(svc::net().wifi().server == 1);
    CHECK(std::strstr(svc::net().wifi().last_fail, "unsynchronised") != nullptr);
    sim::wifi_ntp(s[0], sim::NtpMode::Answer);
}

// The AP goes away: Backoff with the reason, then back when it returns.
void test_wifi_loses_and_regains_the_ap() {
    sim::wifi_remove_ap("home");
    CHECK(until([] { return wstate() == tp::WifiState::Backoff; }));
    CHECK(svc::net().wifi().err == tp::WifiErr::NoAp);
    sim::wifi_add_ap("home", "correct horse", -52);
    hal::host::set_warp(10.0);  // the first retry is 5 s away
    CHECK(until([] { return wstate() == tp::WifiState::Online; }, 5000));
    hal::host::set_warp(1.0);
}

// The rear toggle stops Wi-Fi too, and it comes back by itself.
void test_wifi_radio_off() {
    sim::set_expander_in(hal::expander::Sig::RadioOff, false);
    CHECK(until([] { return wstate() == tp::WifiState::Off; }, 3000));
    RecordingSink r;
    CHECK(run("net sntp sync", r) == Status::NotReady);
    sim::set_expander_in(hal::expander::Sig::RadioOff, true);
    CHECK(until([] { return wstate() == tp::WifiState::Online; }, 3000));
}

// Forget: disconnected, nothing stored, and the knob may set the clock again.
void test_wifi_forget() {
    RecordingSink r;
    CHECK(run("net wifi forget", r) == Status::Ok);
    CHECK(r.contains("forgot \"home\""));
    CHECK(until([] { return wstate() == tp::WifiState::Idle; }));
    CHECK(!svc::net().wifi().provisioned);
    CHECK(until([] { return !svc::chrono().snapshot().net_provisioned; }));
    CHECK(until([] { return !svc::ui().snapshot().net_locked; }, 2000));
    // Refusals.
    RecordingSink bad;
    CHECK(run("net wifi join", bad) == Status::BadArg);
    CHECK(run("net wifi join home short", bad) == Status::BadArg);
    CHECK(run("net wifi join hex:zz", bad) == Status::BadArg);
    CHECK(run("net wifi join 123456789012345678901234567890123 password", bad) == Status::BadArg);
    CHECK(run("net wifi bogus", bad) == Status::BadArg);
}

// The zone commands: a rule, a fixed offset, and the phone's offset agreeing or not.
void test_chrono_zones() {
    auto& c = svc::chrono();
    RecordingSink r;
    CHECK(run("chrono tz CET-1CEST,M3.5.0,M10.5.0/3 Europe/Zurich", r) == Status::Ok);
    // 2026-07-01 10:00Z = 12:00 CEST.
    CHECK(run("chrono time epoch 1782900000000", r) == Status::Ok);
    CHECK(until([] { return svc::chrono().snapshot().hour == 12; }));
    CHECK(c.snapshot().tz_off_min == 120 && c.snapshot().tz_dst);
    CHECK_STREQ(c.snapshot().tz_name, "Europe/Zurich");
    // The phone agreeing with the rule keeps the rule.
    CHECK(run("chrono time epoch 1782900000000 120", r) == Status::Ok);
    CHECK_STREQ(c.snapshot().tz_name, "Europe/Zurich");
    // Disagreeing replaces it with a fixed offset.
    CHECK(run("chrono time epoch 1782900000000 -300", r) == Status::Ok);
    CHECK(until([] { return svc::chrono().snapshot().hour == 5; }));
    CHECK_STREQ(c.snapshot().tz_posix, "<-0500>5");
    CHECK(!c.snapshot().tz_dst);
    RecordingSink show;
    CHECK(run("chrono tz", show) == Status::Ok);
    CHECK(show.contains("posix=<-0500>5"));
    CHECK(show.contains("offset=-300"));
    RecordingSink bad;
    CHECK(run("chrono tz Europe/Zurich", bad) == Status::BadArg);
    CHECK(run("chrono tz PST8PDT,M3.2.0", bad) == Status::BadArg);
    run("chrono tz 0", r);
}

void run_wifi_service_tests() {
    test_wifi_starts_idle();
    test_wifi_join_and_sync();
    test_sntp_falls_back_in_order();
    test_sntp_refuses_unsynced();
    test_wifi_loses_and_regains_the_ap();
    test_wifi_radio_off();
    test_wifi_forget();
    test_chrono_zones();
    sim::wifi_clear();
}
