// The command channel's framing: CLI lines in, CLI output out.     [FIRMWARE.md §8.2]
//
// The app speaks the CLI.  Not a TLV of its own, not a second command set -- the exact line
// you would type on the console, prefixed with a request id so answers can be matched to
// questions.  Everything the console can do the app can do (rule 6), `help` works over the
// air, and a phone with nRF Connect is already a debug terminal.
//
//   request   (write to `cmd`)     "<id> <cli line>"    e.g. "7 motion goto 07:15"
//                                  "<cli line>"         id 0, for hand-typed tests
//   response  (notify on `rsp`)    "<id>|<text>"        one output line
//                                  "<id>+<text>"        a fragment; more of the same line follows
//                                  "<id>=<key>=<value>" machine-readable pair (Sink::kv)
//                                  "<id>$<status>"      terminal; always exactly one, last
//
// `<status>` is clk::name(Status): ok bad-arg denied busy not-ready failed not-present.
// A line longer than one notification is split into `+` fragments ending in a `|` (or `=`),
// so a reader appends until it sees a terminator kind.
#pragma once

#include <cstddef>
#include <cstdint>

namespace clk::transport {

inline constexpr std::size_t kMaxLine = 256;  // the console's own max_cmdline_length

struct Request {
    uint16_t id = 0;
    char line[kMaxLine + 1] = {};
};

// False on an empty line, a line over kMaxLine, an id over 65535, or an embedded NUL.
bool parse_request(const uint8_t* data, std::size_t len, Request& out) noexcept;

enum class Kind : char { Line = '|', More = '+', Kv = '=', Done = '$' };

// One notification's worth of bytes.  Returns false to stop (the link is gone).
using EmitFn = bool (*)(void* ctx, const uint8_t* data, std::size_t len);

// Frames one record into as many notifications of at most `max_frame` bytes as it takes and
// hands each to `emit`.  `kind` is Line, Kv or Done (More is produced here, never asked for).
// Returns the number of frames emitted; stops early if `emit` returns false.
std::size_t emit_record(uint16_t id, Kind kind, const char* text, std::size_t max_frame,
                        EmitFn emit, void* ctx) noexcept;

}  // namespace clk::transport
