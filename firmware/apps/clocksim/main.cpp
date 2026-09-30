// clocksim -- the product on your laptop.                  [FIRMWARE.md D14, §11.2]
//
// Two front-ends onto the same CLI: stdin, and a loopback socket that `ux/` attaches to.
// Both go through cli::dispatch_line, so anything the app can do is something you can type,
// and anything you type the app sees the result of.
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "uibridge.hpp"

#include "clk/cli/console.hpp"
#include "clk/cli/net_bind.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/log.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"
#include "clk/services/net.hpp"
#include "clk/services/storage.hpp"
#include "clk/services/supervisor.hpp"
#include "clk/services/ui.hpp"

namespace {

// `sim reboot`.  execv replaces the whole process image -- every thread, every singleton,
// every service's idea of where the hands are -- which is as close to an NVIC reset as a
// laptop gets.  The bridge's sockets are FD_CLOEXEC, so the port comes back free and the
// attached browser sees a clean disconnect and reconnects itself.
char** g_argv = nullptr;

void reboot_now() {
    CLK_LOGW(sys, "reboot: re-exec %s", g_argv && g_argv[0] ? g_argv[0] : "?");
    // The caller is a CLI thread; give the bridge a moment to push that line to whoever is
    // attached, since nothing survives the exec to explain the silence.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    // stdout and stderr BY NAME.  fflush(nullptr) walks every open FILE and locks each one,
    // and the console thread is parked inside fgets() holding stdin's lock -- so the flush
    // that was meant to tidy up deadlocked the reboot instead, with the CLI mutex held.
    std::fflush(stdout);
    std::fflush(stderr);
    if (g_argv && g_argv[0]) ::execv(g_argv[0], g_argv);
    CLK_LOGE(sys, "reboot: execv failed (%s) -- still the old image", std::strerror(errno));
}

// Where the NVS stand-in lives.  A real clock's calibration survives the power cord being
// pulled, so the sim's has to survive a restart too -- `sim reset`, `sys reboot` and closing
// the terminal all leave this file exactly where it was.
std::string store_path() {
    if (const char* env = std::getenv("CLOCKSIM_NVS")) return env;
    const char* home = std::getenv("HOME");
    return home ? std::string{home} + "/.clocksim.nvs" : std::string{".clocksim.nvs"};
}

// The microSD slot.  A directory stands in for the card -- /sd/tones/x.wav is <dir>/tones/x.wav
// -- and no directory is an empty slot, which is a state the product must handle anyway.
std::string sd_path() {
    if (const char* env = std::getenv("CLOCKSIM_SD")) return env;
    const char* home = std::getenv("HOME");
    return home ? std::string{home} + "/.clocksim.sd" : std::string{};
}

void usage() {
    std::printf(
        "clocksim -- the clock firmware against fake hardware\n"
        "  --ui-port <n>   serve the ux/ bridge on 127.0.0.1:<n>  (default %u)\n"
        "  --no-ui         console only\n"
        "  --no-home       do not home on boot (the test rig; `motion home` still works)\n"
        "  --nvs <path>    where persistent settings live  (default ~/.clocksim.nvs,\n"
        "                  or $CLOCKSIM_NVS)\n"
        "  --sd <dir>      the microSD card: a directory, tones in <dir>/tones/\n"
        "                  (default ~/.clocksim.sd, or $CLOCKSIM_SD; missing = no card)\n"
        "  -h, --help      this\n",
        clk::uibridge::kDefaultPort);
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t ui_port = clk::uibridge::kDefaultPort;
    bool home_on_boot = true;
    std::string nvs = store_path();
    std::string sd = sd_path();
    g_argv = argv;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--no-ui") == 0) {
            ui_port = 0;
        } else if (std::strcmp(a, "--no-home") == 0) {
            home_on_boot = false;
        } else if (std::strcmp(a, "--sd") == 0 && i + 1 < argc) {
            sd = argv[++i];
        } else if (std::strcmp(a, "--nvs") == 0 && i + 1 < argc) {
            nvs = argv[++i];
        } else if (std::strcmp(a, "--ui-port") == 0 && i + 1 < argc) {
            ui_port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            usage();
            return 0;
        } else {
            std::printf("unknown argument: %s\n", a);
            usage();
            return 2;
        }
    }

    clk::log::init(clk::log::Level::Info);
    clk::hal::init();
    clk::hal::host::set_reboot_hook(reboot_now);
    clk::hal::host::set_store_path(nvs.c_str());
    CLK_LOGI(sys, "nvs: %s", nvs.c_str());
    clk::hal::host::set_sd_dir(sd.c_str());
    CLK_LOGI(sys, "sd: %s", sd.c_str());

    // Same construction order as app_main: wire the AOs to each other, then start them in
    // priority order.  motion has no dependencies; chrono drives it; ui drives both.
    auto& motion = clk::svc::motion();
    auto& chrono = clk::svc::chrono();
    auto& ui = clk::svc::ui();
    auto& net = clk::svc::net();
    auto& storage = clk::svc::storage();
    auto& sup = clk::svc::supervisor();
    motion.subscribe(&chrono);
    chrono.bind(&motion);
    ui.bind(&motion, &chrono, &net, &storage);
    net.bind(&motion, &chrono, &ui, &storage);
    clk::cli::bind_net();
    // Watching, never restarting: a re-exec'd clocksim under a test rig is not a recovery.
    sup.bind(&motion, &chrono, &storage);
    for (clk::ActiveObject* ao :
         {static_cast<clk::ActiveObject*>(&motion), static_cast<clk::ActiveObject*>(&chrono),
          static_cast<clk::ActiveObject*>(&storage), static_cast<clk::ActiveObject*>(&net),
          static_cast<clk::ActiveObject*>(&ui)})
        sup.watch(ao);
    // A real clock homes the moment it powers up (§6.1).  The test rig turns that off: a
    // nine-second sweep before every case buys nothing there, and the case that is ABOUT
    // boot homing simply starts a clocksim without the flag.
    motion.set_home_on_start(home_on_boot);
    motion.start();
    chrono.start();
    storage.start();  // before ui: an alarm due at boot must find the card already mounted
    net.start();
    ui.start();
    sup.start();

    clk::uibridge::start(ui_port);
    clk::cli::console_run();
    clk::uibridge::stop();

    sup.stop();
    ui.stop();
    net.stop();
    storage.stop();
    chrono.stop();
    motion.stop();
    return 0;
}
