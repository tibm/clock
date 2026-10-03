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
#include "clk/evtrace.hpp"
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

void banner() {
    const auto& b = clk::cli::build_info();
    CLK_LOGI(sys, "clock %s %s  %s/%s  idf %s", b.app_version, b.git_sha, b.profile, b.board,
             b.sdk);
    const auto rst = static_cast<uint8_t>(esp_reset_reason());
    const auto ev = clk::evtrace::stats();
    CLK_LOGI(sys, "reset reason %u (%s); event ring %s, %u carried", rst,
             clk::hal::sys::reset_name(rst), ev.warm ? "warm" : "cold",
             static_cast<unsigned>(ev.carried));
    // §6.8: the panic handler's post-mortem, said once per boot until erased -- it lands in
    // the journal and so on the card, which is where it gets read in a closed box.
    clk::hal::sys::Coredump cd{};
    if (clk::hal::sys::coredump(cd) == clk::Status::Ok && cd.present) {
        CLK_LOGW(sys, "coredump on flash: %u B, %s; task '%s' pc 0x%08x cause %u -- `sys coredump`",
                 static_cast<unsigned>(cd.size), cd.valid ? "valid" : "CORRUPT", cd.task,
                 static_cast<unsigned>(cd.pc), static_cast<unsigned>(cd.cause));
        if (cd.reason[0]) CLK_LOGW(sys, "coredump reason: %s", cd.reason);
    }
}

}  // namespace

extern "C" void app_main(void) {
    // First: adopt whatever the last boot's log ring still holds (a panic, the watchdog) before
    // this boot writes a single line into it.  `storage` takes it to the card (§9.4a).
    clk::journal::init();
    clk::evtrace::init();  // likewise the event ring, before any AO dispatches into it
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
    ui.bind(&motion, &chrono, &net, &storage, &sup);
    net.bind(&motion, &chrono, &ui, &storage);
    clk::cli::bind_net();
    sup.bind(&motion, &chrono, &storage, &ui);
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
