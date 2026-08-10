// Serialising the two host front-ends.                     [FIRMWARE.md §9.1, §11.2]
//
// HOST ONLY -- defined in console_host.cpp, and nothing in the target build includes this.
// On the host, stdin and the UI bridge socket both call dispatch_line(), and dispatch()
// carries state (the sliding `unsafe` window).  One mutex, taken by both, and the CLI stays
// what it is: one command at a time.
//
// The target does not need it *yet* only because the `cli` AO is the sole dispatcher there.
// The moment transport/ble_gatt lands (rule 6), `net` on core 0 will dispatch concurrently
// with `cli` on core 1 and will need the same lock for real.
#pragma once

#include <mutex>

namespace clk::cli::host {

// Timed rather than plain: a `sensor ... stream` on stdin holds this for up to 120 s, and
// the UI bridge would rather answer Busy than stall its state frames behind it.
std::timed_mutex& dispatch_mutex() noexcept;

}  // namespace clk::cli::host
