// clocksim -- the product on your laptop.                  [FIRMWARE.md D14, §11.2]
//
// Two front-ends onto the same CLI: stdin, and a loopback socket that `ux/` attaches to.
// Both go through cli::dispatch_line, so anything the app can do is something you can type,
// and anything you type the app sees the result of.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "uibridge.hpp"

#include "clk/cli/console.hpp"
#include "clk/hal/hal.hpp"
#include "clk/log.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"
#include "clk/services/ui.hpp"

namespace {

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
