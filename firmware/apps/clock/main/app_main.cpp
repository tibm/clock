// app_main: construct and start, nothing else.             [FIRMWARE.md §2]
//
// Scaffold stage -- logging + console only.  The nine AOs are constructed here as they
// land (§3.2), in priority order, after the console so that a failure during AO start is
// something you can actually see and debug.
#include <initializer_list>

#include "esp_system.h"
#include "nvs_flash.h"

#include "clk/cli/console.hpp"
#include "clk/cli/net_bind.hpp"
#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/journal.hpp"
#include "clk/log.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"
#include "clk/services/net.hpp"
#include "clk/services/storage.hpp"
#include "clk/services/supervisor.hpp"
#include "clk/services/ui.hpp"

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

const char* reset_name(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:
            return "power-on";
        case ESP_RST_SW:
            return "sw restart";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_INT_WDT:
            return "INTERRUPT WATCHDOG";
        case ESP_RST_TASK_WDT:
            return "TASK WATCHDOG";
        case ESP_RST_WDT:
            return "WATCHDOG";
        case ESP_RST_DEEPSLEEP:
            return "deep sleep";
        case ESP_RST_BROWNOUT:
            return "BROWN-OUT";
        case ESP_RST_USB:
            return "usb";
        case ESP_RST_JTAG:
            return "jtag";
        case ESP_RST_CPU_LOCKUP:
            return "CPU LOCKUP";
        default:
            return "other";
    }
}

void banner() {
    const auto& b = clk::cli::build_info();
    CLK_LOGI(sys, "clock %s %s  %s/%s  idf %s", b.app_version, b.git_sha, b.profile, b.board,
             b.sdk);
    CLK_LOGI(sys, "reset reason %d (%s)", static_cast<int>(esp_reset_reason()),
             reset_name(esp_reset_reason()));
}

}  // namespace

extern "C" void app_main(void) {
    // First: adopt whatever the last boot's log ring still holds (a panic, the watchdog) before
    // this boot writes a single line into it.  `storage` takes it to the card (§9.4a).
    clk::journal::init();
    clk::log::init(Level::Info);
    init_nvs();
    banner();
    clk::hal::init();

    // Milestone 0 (§12.1): the console comes up before anything else, so everything after
    // it is debuggable from the moment it exists.
    clk::cli::console_run();

    // Then the AOs, wired to each other and started in priority order.  On this board the
    // HAL is still stubs, so every peripheral call answers NotPresent and each AO says so
    // once and stays quiet (D16) -- which is the correct behaviour for a devkit with
    // nothing soldered to it, and is exactly what §12.0's bring-up order then fills in.
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
    sup.bind(&motion, &chrono, &storage);
    for (clk::ActiveObject* ao :
         {static_cast<clk::ActiveObject*>(&motion), static_cast<clk::ActiveObject*>(&chrono),
          static_cast<clk::ActiveObject*>(&storage), static_cast<clk::ActiveObject*>(&net),
          static_cast<clk::ActiveObject*>(&ui)})
        sup.watch(ao);
    sup.set_restart_on_stall(true);
    motion.start();
    chrono.start();
    storage.start();  // before ui: an alarm due at boot must find the card already mounted
    net.start();      // before ui: a ten-second hold must find the radio already up
    ui.start();
    sup.start();  // last: it watches the others
}
