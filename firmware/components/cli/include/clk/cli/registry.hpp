// The command registry: lookup, help generation, and the `unsafe` gate.
// [FIRMWARE.md §9.2, §9.6]
#pragma once

#include <cstddef>
#include <string_view>

#include "clk/cli/cmd_spec.hpp"

namespace clk::cli {

// Tables are assembled statically at link time in registry.cpp -- no heap, no static-init
// ordering games, and `help` walks the same rows the parser matches (D13).

// Longest-match lookup over group[/object]/verb.  On success `first` is set to the index of
// the first real argument.  Returns nullptr if nothing matched.
[[nodiscard]] const CmdSpec* find(int argc, const char* const* argv, int& first) noexcept;

// Tokenise + resolve aliases + look up + authorize + run.  This is the whole CLI.
Status dispatch(int argc, const char* const* argv, Sink&) noexcept;
Status dispatch_line(char* line, Sink&) noexcept;   // mutates `line` while tokenising

// `unsafe on|off`, auto-expiring 60 s after the last unsafe command.  [§9.6]
void   unsafe_set(bool on) noexcept;
[[nodiscard]] bool unsafe_active() noexcept;

// Monotonic milliseconds -- supplied by the platform so core stays IDF-free.
using MillisFn = uint32_t (*)();
void set_millis_fn(MillisFn) noexcept;

// Generated help.  group == nullptr lists the groups.  [§9.2]
void help(Sink&, const char* group = nullptr, const char* verb = nullptr) noexcept;

// Build identity, printed by `sys ver` and the boot banner.
struct BuildInfo {
    const char* app_version;
    const char* git_sha;
    const char* build_utc;
    const char* profile;   // "dev" | "release"
    const char* board;     // "devkit" | "rev0_3" | "host"
    const char* sdk;       // "v5.5.5" | "host"
};
[[nodiscard]] BuildInfo const& build_info() noexcept;

}  // namespace clk::cli
