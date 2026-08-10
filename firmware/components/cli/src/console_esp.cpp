// Target console: esp_console REPL over USB-Serial-JTAG.    [FIRMWARE.md §9.1]
//
// This is the ONLY file in the tree that calls esp_console_cmd_register (rule 10).  It
// registers one esp_console command per CLI group and forwards argc/argv straight into
// clk::cli::dispatch, so the group table stays the single source of truth (D13).
#include <cstdio>
#include <cstring>

#include "esp_console.h"
#include "esp_timer.h"
#include "linenoise/linenoise.h"

#include "clk/cli/console.hpp"
#include "clk/cli/registry.hpp"
#include "clk/log.hpp"

namespace clk::cli {
namespace {

uint32_t esp_millis() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

class ConsoleSink final : public Sink {
public:
    void line(const char* t) override { std::printf("%s\n", t); }
    void kv(const char* k, const char* v) override { std::printf("%s=%s\n", k, v); }
    void done(Status s) override {
        if (s != Status::Ok) std::printf("[%s]\n", cmd::name(s));
        std::fflush(stdout);
    }
};

int forward(int argc, char** argv) {
    ConsoleSink sink;
    return static_cast<int>(dispatch(argc, argv, sink));
}

// Every distinct group name in the table, plus the aliases, gets one esp_console entry.
constexpr const char* kGroups[] = {
    "help", "?", "unsafe", "sys",
    // as groups land they are added here; the CmdSpec table stays authoritative for the
    // verbs, this list only tells linenoise which first words exist.
    "motion", "hand", "ui", "led", "audio", "snd", "board", "i2c",
    "chrono", "time", "storage", "fs", "net", "sensor", "sim",
};

}  // namespace

void console_run() {
    set_millis_fn(esp_millis);

    esp_console_repl_t* repl = nullptr;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt          = "clock>";
    repl_cfg.max_cmdline_length = 256;
    repl_cfg.task_stack_size = 6 * 1024;      // §3.2: the cli AO stack
    repl_cfg.task_priority   = 3;

    esp_console_dev_usb_serial_jtag_config_t dev_cfg =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl));

    for (const char* g : kGroups) {
        const esp_console_cmd_t cmd{
            .command = g,
            .help    = nullptr,          // `help` generates it from the CmdSpec table
            .hint    = nullptr,
            .func    = &forward,
            .argtable = nullptr,
            .func_w_context = nullptr,
            .context = nullptr,
        };
        // A group with no rows yet is simply not registered.
        (void)esp_console_cmd_register(&cmd);
    }

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

}  // namespace clk::cli
