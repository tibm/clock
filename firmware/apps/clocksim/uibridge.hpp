// The fake hardware's window onto `ux/`.                   [FIRMWARE.md D14, §11.2]
//
// Part of the clocksim APP, not a component -- deliberately.  A component under
// `components/` is visible to `EXTRA_COMPONENT_DIRS` and could be linked into the product by
// accident; here that is structurally impossible.  Nothing in services/ or drivers/ may
// include it, for the same reason nothing may include `clk/hal/host/sim.hpp`: code that can
// detect the fakes is code that will eventually special-case them, and D14 stops being true.
//
// The protocol is deliberately asymmetric:
//
//   ux -> clocksim   one line of CLI text per newline, optionally `#<id> ` prefixed.
//                    `sim knob -12`, `motion goto 07:30` -- anything you could type.
//                    So there is no JSON parser in the firmware, and every input the app
//                    can produce is one a human can reproduce at the console (D9, D13).
//
//   clocksim -> ux   newline-delimited JSON: one `hello`, then `state` at 50 Hz, plus
//                    `log` and `res` as they happen.
//
// Frame shapes are in apps/clocksim/README.md.
#pragma once

#include <cstdint>

#include "clk/status.hpp"

namespace clk::uibridge {

inline constexpr uint16_t kDefaultPort = 4747;
inline constexpr int kProtoVersion = 1;

// Binds 127.0.0.1:port and serves until stop().  port == 0 disables the bridge entirely.
// A bind failure is reported and is NOT fatal: clocksim is still a working console without
// a UI attached, and refusing to boot because a stale copy holds the port would be rude.
Status start(uint16_t port) noexcept;
void stop() noexcept;

[[nodiscard]] bool running() noexcept;
[[nodiscard]] uint16_t port() noexcept;

}  // namespace clk::uibridge
