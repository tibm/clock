// The one result type every Command returns.               [FIRMWARE.md §5, D16]
#pragma once

#include <cstdint>

namespace clk::cmd {

enum class Status : uint8_t {
    Ok = 0,
    BadArg,      // the caller's fault: unparseable, out of range
    Denied,      // authorization: wrong Origin, or `unsafe` is off
    Busy,        // owner is mid-operation and will not queue this
    NotReady,    // owner exists but is not initialised yet (e.g. pre-home)
    Failed,      // the operation was attempted and did not work
    NotPresent,  // D16: the hardware is absent.  NOT an error -- half of bring-up is
                 // deliberately running with things unplugged.  Never logged as an error,
                 // always surfaced to the CLI so `sensor list` can say so plainly.
};

const char* name(Status) noexcept;

}  // namespace clk::cmd
