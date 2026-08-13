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
#include <thread>

#include "uibridge.hpp"

#include "clk/cli/console.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/log.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"
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

void usage() {
    std::printf(
        "clocksim -- the clock firmware against fake hardware\n"
        "  --ui-port <n>   serve the ux/ bridge on 127.0.0.1:<n>  (default %u)\n"
        "  --no-ui         console only\n"
        "  -h, --help      this\n",
        clk::uibridge::kDefaultPort);
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t ui_port = clk::uibridge::kDefaultPort;
    g_argv = argv;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--no-ui") == 0) {
            ui_port = 0;
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

    // Same construction order as app_main: wire the AOs to each other, then start them in
    // priority order.  motion has no dependencies; chrono drives it; ui drives both.
    auto& motion = clk::svc::motion();
    auto& chrono = clk::svc::chrono();
    auto& ui = clk::svc::ui();
    motion.subscribe(&chrono);
    chrono.bind(&motion);
    ui.bind(&motion, &chrono);
    motion.start();
    chrono.start();
    ui.start();

    clk::uibridge::start(ui_port);
    clk::cli::console_run();
    clk::uibridge::stop();

    ui.stop();
    chrono.stop();
    motion.stop();
    return 0;
}
