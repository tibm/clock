// Hands the `net` AO the two things it needs from the CLI layer: the line dispatcher (so a
// phone runs exactly the console's commands) and the build identity (the `info`
// characteristic and every snapshot's fw_id).  services/ cannot reach up for either (§2), so
// app_main, clocksim and the host tests each call this once, before net().start().
#pragma once

namespace clk::cli {
void bind_net() noexcept;
}  // namespace clk::cli
