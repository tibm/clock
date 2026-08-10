// One table drives the parser, `help`, and tab-completion.  [FIRMWARE.md D13, §9.2]
//
// Grammar:  <group> [object] <verb> [args] [--flags]
//
// Adding a command means adding a row -- never a call to esp_console_cmd_register (rule 10).
// `help` is generated from these rows, so it cannot drift from what the parser accepts.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "clk/command/sink.hpp"
#include "clk/command/status.hpp"

#ifndef CLK_HAVE_SIM
#define CLK_HAVE_SIM 0
#endif

namespace clk::cli {

using cmd::Sink;
using cmd::Status;

// Wildcard object: `sensor <name> read` matches any <name> and passes it in Args::obj.
inline constexpr const char* kAnyObject = "*";

enum Flags : uint16_t {
    None = 0,
    Unsafe = 1u << 0,     // needs `unsafe on`; compiled out when CONFIG_CLOCK_CLI_UNSAFE=n
    ReleaseOk = 1u << 1,  // survives into release builds -- the field diagnostics
    Streaming = 1u << 2,  // produces a bounded stream; never runs in the cli task (rule 12)
    HostOnly = 1u << 3,   // clocksim only (e.g. `sim warp`)
};

// Tokenised command line.  argv[0] is the group, so a handler for `ui led test` sees
// argc>=3 with argv[0]="ui", argv[1]="led", argv[2]="test".  Use rest()/arg() to skip that.
struct Args {
    int argc = 0;
    const char* const* argv = nullptr;
    int first = 0;              // index of the first real argument, set by the parser
    const char* obj = nullptr;  // the matched object token, for wildcard rows

    [[nodiscard]] int count() const noexcept { return argc - first; }
    [[nodiscard]] const char* arg(int i) const noexcept {
        const int k = first + i;
        return (k >= 0 && k < argc) ? argv[k] : nullptr;
    }
    [[nodiscard]] std::string_view sv(int i) const noexcept {
        const char* p = arg(i);
        return p ? std::string_view{p} : std::string_view{};
    }
    [[nodiscard]] bool has_flag(std::string_view f) const noexcept {
        for (int i = 0; i < argc; ++i) {
            if (std::string_view{argv[i]} == f) return true;
        }
        return false;
    }
};

struct CmdSpec {
    const char* group;  // "ui"
    // "led" for a fixed object; kAnyObject to match any token (the sensor name in
    // `sensor homing stream`), which then arrives in Args::obj; nullptr for a bare verb.
    const char* object;
    const char* verb;  // "test"
    const char* args;  // "[<ms>]"   -- shown verbatim in help
    const char* help;  // one imperative line, <= 60 chars
    uint16_t flags;
    Status (*run)(Args const&, Sink&);
};

// A group's rows, contributed by one .cpp file.
struct CmdTable {
    const CmdSpec* rows;
    std::size_t count;
};

}  // namespace clk::cli
