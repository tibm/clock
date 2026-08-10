#include <cstdarg>
#include <cstdio>

#include "clk/command/sink.hpp"
#include "clk/command/status.hpp"

namespace clk::cmd {

const char* name(Status s) noexcept {
    switch (s) {
        case Status::Ok:         return "ok";
        case Status::BadArg:     return "bad-arg";
        case Status::Denied:     return "denied";
        case Status::Busy:       return "busy";
        case Status::NotReady:   return "not-ready";
        case Status::Failed:     return "failed";
        case Status::NotPresent: return "not-present";
    }
    return "?";
}

void Sink::vprintf(const char* fmt, std::va_list ap) {
    char buf[256];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    line(buf);
}

void Sink::printf(const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

}  // namespace clk::cmd
