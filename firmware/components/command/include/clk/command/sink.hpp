// Where a command's output goes.                            [FIRMWARE.md §5]
//
// The CLI writes lines to the console, BLE packs a notification, a host test appends to a
// vector and asserts on it.  Nothing above this interface knows which -- that is what lets
// one command implementation serve the console, the phone app and the test suite.
#pragma once

#include <cstdarg>

#include "clk/command/status.hpp"

namespace clk::cmd {

class Sink {
public:
    virtual ~Sink() = default;

    // Human-readable output.  One call per line, no trailing newline.
    virtual void line(const char* text) = 0;

    // Machine-readable output: `sensor ... stream --csv` and the BLE TLV encoder use this
    // instead of line().  Kept printf-shaped rather than typed for now; it becomes a
    // Value variant when the sensor set is real.
    virtual void kv(const char* key, const char* value) = 0;

    // Terminal status.  Exactly one call, always.
    virtual void done(Status) = 0;

    // Convenience -- implemented in terms of line().
    void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void vprintf(const char* fmt, std::va_list);
};

}  // namespace clk::cmd
