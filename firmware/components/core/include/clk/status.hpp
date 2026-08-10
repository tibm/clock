// The one result vocabulary, shared by every layer.        [FIRMWARE.md §5, D16]
//
// Lives in core/ rather than command/ because hal/ and drivers/ sit BELOW command in the
// dependency graph (§2) and still need to say "not present".  command/ re-exports it.
#pragma once

#include <cstdint>

namespace clk {

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

// Value-or-status.  Deliberately not std::expected: -fno-exceptions, and this needs to
// compile identically on the host and on GCC 14 for xtensa with no library surprises.
template <class T>
struct Result {
    Status st = Status::Ok;
    T      v{};

    [[nodiscard]] constexpr bool ok() const noexcept { return st == Status::Ok; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok(); }

    static constexpr Result good(T value) noexcept { return Result{Status::Ok, value}; }
    static constexpr Result bad(Status s) noexcept { return Result{s, T{}}; }
};

}  // namespace clk
