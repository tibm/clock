// Platform console entry point.  Implemented by console_esp.cpp or console_host.cpp;
// selected by CMake, never by #ifdef in a caller.        [FIRMWARE.md §2, §9.1]
#pragma once

namespace clk::cli {

// Starts the REPL.  On target this returns after esp_console_start_repl() spawns its task;
// on the host it blocks until EOF.
void console_run();

}  // namespace clk::cli
