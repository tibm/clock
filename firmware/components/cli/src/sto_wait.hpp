// Waiting on a `storage` request, for the three groups that queue one (storage, audio, chrono).
#pragma once

#include <cstdint>

#include "clk/cli/cmd_spec.hpp"
#include "clk/services/storage.hpp"

namespace clk::cli {

// `seq` is what the Storage request returned.  Waits for the AO to answer it -- a mount, an
// open and a header read, so tens of milliseconds on a card -- and prints the refusal, by
// name, when there is one.  `what` heads that line ("play", "tone").  Ok means the AO did it.
Status sto_await(uint32_t seq, const char* what, Sink& out) noexcept;

// "3.2 s" / "1 min 04 s" -- how a tone's length reads in a listing.
void fmt_ms(char* buf, std::size_t cap, uint32_t ms) noexcept;

}  // namespace clk::cli
