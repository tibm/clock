#include <cstdarg>
#include <cstdio>

#include "clk/command/sink.hpp"

namespace clk::cmd {

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
