// Re-export of the shared Status vocabulary.               [FIRMWARE.md §5, D16]
// The definition lives in core/ because hal/ and drivers/ sit below command/ in the
// dependency graph (§2) and must be able to return NotPresent.
#pragma once

#include "clk/status.hpp"

namespace clk::cmd {

using clk::Status;
using clk::name;
using clk::Result;

}  // namespace clk::cmd
