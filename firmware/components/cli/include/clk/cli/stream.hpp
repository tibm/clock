// Bounded sensor streaming.                                [FIRMWARE.md §9.5, rule 12]
#pragma once

#include <cstddef>
#include <cstdint>

#include "clk/command/sink.hpp"

namespace clk::cli {

using cmd::Sink;
using cmd::Status;

// Produces one sample as "key=value key=value".  Runs on the producer thread, never on the
// console's.  Returns NotPresent to end the stream early and quietly (D16).
using SampleFn = Status (*)(char* out, std::size_t cap);

struct StreamOpts {
    uint16_t hz = 10;
    uint32_t secs = 10;  // TTL -- a stream must not outlive your attention
    bool csv = false;
    bool keep_logs = false;  // by default a stream quiets the log to `warn`
};

inline constexpr uint16_t kMaxHz = 200;
inline constexpr uint32_t kMaxSecs = 120;

// Blocks the console for the duration, draining what the producer generates.  The producer
// samples on its own cadence and drops rather than back-pressuring: a slow console must
// never stall `board` and must never stall `motion`.
Status run_stream(const char* name, SampleFn, StreamOpts const&, Sink&);

}  // namespace clk::cli
