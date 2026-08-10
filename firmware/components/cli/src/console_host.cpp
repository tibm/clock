// Host console: the same CmdSpec table on stdin/stdout.     [FIRMWARE.md §9.1, §11.2]
//
// No linenoise, no IDF -- everything above this file is identical to the target build,
// which is the whole point of clocksim.
#include <chrono>
#include <cstdio>
#include <cstring>

#include "clk/cli/console.hpp"
#include "clk/cli/registry.hpp"
#include "clk/log.hpp"

namespace clk::cli {
namespace {

uint32_t host_millis() noexcept {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

class StdioSink final : public Sink {
public:
    void line(const char* t) override { std::printf("%s\n", t); }
    void kv(const char* k, const char* v) override { std::printf("%s=%s\n", k, v); }
    void done(Status s) override {
        if (s != Status::Ok) std::printf("[%s]\n", cmd::name(s));
        std::fflush(stdout);
    }
};

}  // namespace

void console_run() {
    set_millis_fn(host_millis);

    const auto& b = build_info();
    std::printf("clock-sim %s  (hal=fake, board=%s, profile=%s)  type `help`\n",
                b.app_version, b.board, b.profile);

    StdioSink sink;
    char line[256];
    while (true) {
        std::printf("> ");
        std::fflush(stdout);
        if (!std::fgets(line, sizeof line, stdin)) break;      // EOF / ^D
        line[std::strcspn(line, "\r\n")] = '\0';
        if (std::strcmp(line, "quit") == 0 || std::strcmp(line, "exit") == 0) break;
        dispatch_line(line, sink);
    }
    std::printf("\nbye\n");
}

}  // namespace clk::cli
