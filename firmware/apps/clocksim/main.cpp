// clocksim -- the product on your laptop.                  [FIRMWARE.md D14, §11.2]
//
// Scaffold stage: core + command + cli only.  Services, the host port and the fake HAL
// land on top of this without changing the file.
#include "clk/cli/console.hpp"
#include "clk/log.hpp"

int main() {
    clk::log::init(clk::log::Level::Info);
    CLK_LOGI(sys, "clocksim starting");
    clk::cli::console_run();
    return 0;
}
