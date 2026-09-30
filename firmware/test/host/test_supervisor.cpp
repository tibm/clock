// The debug journal on the card, and the supervisor catching stuck hands.  [FIRMWARE.md §9.4a,
// §6.8]
//
// Called from run_motion_service_tests() with every AO -- the supervisor included -- running.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "check.hpp"
#include "testutil.hpp"

#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/journal.hpp"
#include "clk/log.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"
#include "clk/services/storage.hpp"
#include "clk/services/supervisor.hpp"

using namespace clk;
namespace sim = hal::host;

namespace {

template <class Fn>
bool wait_until(Fn pred, int max_ms = 4000) {
    for (int i = 0; i < max_ms / 5; ++i) {
        if (pred()) return true;
        hal::clock_::sleep_ms(5);
    }
    return pred();
}

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Every file in <card>/debug, concatenated, with "\n#<name>\n" before each.
std::string debug_dir(const std::string& card) {
    std::string all;
    const std::string dir = card + "/debug";
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            all += "\n#";
            all += e->d_name;
            all += "\n";
            all += slurp(dir + "/" + e->d_name);
        }
        ::closedir(d);
    }
    return all;
}

// A line logged now is in this boot's file after one flush, under a header.
void test_journal_reaches_the_card(const std::string& card) {
    CLK_LOGW(sys, "journal-probe-7f3a");
    RecordingSink r;
    CHECK(run("sys journal flush", r) == Status::Ok);
    const auto d = svc::storage().dbg_snapshot();
    CHECK(d.file[0] != '\0');
    const auto text = debug_dir(card);
    CHECK(text.find("journal-probe-7f3a") != std::string::npos);
    CHECK(text.find("=== boot ") != std::string::npos);
    RecordingSink st;
    CHECK(run("sys journal", st) == Status::Ok);
    CHECK(st.contains("/sd/debug"));
}

// A warm reset, as far as a host can have one: the ring is adopted by a "new boot" while it
// still holds lines, and `storage` comes back up.  Those lines go to the PREVIOUS boot's file,
// under the marker -- and this boot's lines to its own.
void test_journal_rescues_the_last_lines(const std::string& card) {
    auto& sto = svc::storage();
    sto.ActiveObject::stop();  // the AO -- Storage::stop() is "stop playing"
    // A real NVS for the boot counter, seeded: this boot is 41, the next one 42.
    char nvs[] = "/tmp/clk-dbg-nvs-XXXXXX";
    const int fd = ::mkstemp(nvs);
    CHECK(fd >= 0);
    if (fd >= 0) ::close(fd);
    sim::set_store_path(nvs);
    CHECK(hal::store::set_i32("sys.boot", 41) == Status::Ok);
    const uint32_t boot0 = 41;
    const char kLast[] = "E (1) motion: the-last-line-before-the-hang\n";
    journal::write(kLast, sizeof kLast - 1);
    journal::init();
    sto.start();
    CHECK(wait_until([&] { return sto.dbg_snapshot().boot == boot0 + 1; }, 2000));
    CLK_LOGI(sys, "first-line-after-the-reset");
    RecordingSink r;
    CHECK(run("sys journal flush", r) == Status::Ok);

    char prev[16], cur[16];
    std::snprintf(prev, sizeof prev, "%06u.log", static_cast<unsigned>(boot0 % 1000000u));
    std::snprintf(cur, sizeof cur, "%06u.log", static_cast<unsigned>((boot0 + 1) % 1000000u));
    const auto p = slurp(card + "/debug/" + prev);
    const auto c = slurp(card + "/debug/" + cur);
    const auto mark = p.find("--- the last lines before the reset");
    CHECK(mark != std::string::npos);
    CHECK(p.find("the-last-line-before-the-hang") != std::string::npos);
    CHECK(p.find("the-last-line-before-the-hang") > mark);
    CHECK(c.find("the-last-line-before-the-hang") == std::string::npos);
    CHECK(c.find("first-line-after-the-reset") != std::string::npos);
    CHECK(sto.dbg_snapshot().recovered >= sizeof kLast - 1);
    sim::set_store_path("");
    ::unlink(nvs);
}

// The failure this exists for: every AO alive, the clock ticking, and the hands not moving.
void test_supervisor_catches_stuck_hands() {
    using svc::Motion;
    auto& mo = svc::motion();
    auto& sup = svc::supervisor();
    sim::reset();
    cli::unsafe_set(true);
    sim::set_warp(20.0);
    RecordingSink r;
    run("motion tune autohome 0", r);
    mo.home();
    // homed is still true from an earlier case until the request is picked up.
    CHECK(wait_until([&] { return mo.snapshot().state == Motion::State::Homing; }, 1000));
    CHECK(wait_until([&] { return mo.snapshot().homed; }, 8000));
    CHECK(run("chrono time set 07:38", r) == Status::Ok);
    CHECK(wait_until([&] { return mo.snapshot().state == Motion::State::Idle; }, 6000));
    sim::set_warp(1.0);
    CHECK(!sup.snapshot().stalled);
    const uint32_t stalls0 = sup.snapshot().hand_stalls;

    sim::set_motor_jam(true);
    // Four minutes, a minute at a time: every step moves chrono's target and each one goes
    // unanswered.  The supervisor ticks in between.
    for (int i = 0; i < 4 && !sup.snapshot().stalled; ++i) {
        sim::advance(60'000'000);
        hal::clock_::sleep_ms(150);
    }
    CHECK(wait_until([&] { return sup.snapshot().stalled; }, 3000));
    CHECK(sup.snapshot().hand_stalls == stalls0 + 1);
    RecordingSink wd;
    CHECK(run("sys wd", wd) == Status::Ok);
    CHECK(wd.contains("STALLED"));

    // Unjammed, the next minute moves the hands and the episode ends.
    sim::set_motor_jam(false);
    sim::set_warp(20.0);
    sim::advance(60'000'000);
    CHECK(wait_until([&] { return !sup.snapshot().stalled; }, 6000));
    CHECK(sup.snapshot().hand_stalls == stalls0 + 1);
    sim::set_warp(1.0);
}

}  // namespace

void run_supervisor_service_tests() {
    char tmpl[] = "/tmp/clk-dbg-XXXXXX";
    const char* d = ::mkdtemp(tmpl);
    CHECK(d != nullptr);
    if (!d) return;
    const std::string card = d;
    sim::set_sd_dir(d);
    RecordingSink r;
    run("storage sd mount", r);
    test_journal_reaches_the_card(card);
    test_journal_rescues_the_last_lines(card);
    test_supervisor_catches_stuck_hands();
    // The stall is in the file too.
    RecordingSink f;
    CHECK(run("sys journal flush", f) == Status::Ok);
    CHECK(debug_dir(card).find("HANDS STALLED") != std::string::npos);
    run("storage sd unmount", r);
    sim::set_sd_dir("");
}
