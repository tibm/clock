// app_main: construct and start, nothing else.             [FIRMWARE.md §2]
//
// Scaffold stage -- logging + console only.  The nine AOs are constructed here as they
// land (§3.2), in priority order, after the console so that a failure during AO start is
// something you can actually see and debug.
#include "esp_system.h"
#include "nvs_flash.h"

#include "clk/cli/console.hpp"
#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/log.hpp"

using clk::log::Level;

namespace {

void init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void banner() {
    const auto& b = clk::cli::build_info();
    CLK_LOGI(sys, "clock %s %s  %s/%s  idf %s", b.app_version, b.git_sha, b.profile, b.board,
             b.sdk);
    CLK_LOGI(sys, "reset reason %d", static_cast<int>(esp_reset_reason()));
}

}  // namespace

extern "C" void app_main(void) {
    clk::log::init(Level::Info);
    init_nvs();
    banner();
    clk::hal::init();

    // Milestone 0 (§12.1): the console comes up before anything else, so everything after
    // it is debuggable from the moment it exists.
    clk::cli::console_run();
}
