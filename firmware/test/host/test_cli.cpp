// CLI registry -- parsing, aliases, the unsafe gate, generated help.
// [FIRMWARE.md §11.1, §9.2, §9.6]
#include "check.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "clk/cli/registry.hpp"
#include "clk/log.hpp"

using namespace clk;
using cli::Status;

namespace {

// A Sink that records instead of printing -- the seam that makes the CLI host-testable.
class RecordingSink final : public cmd::Sink {
public:
    std::vector<std::string> lines;
    Status                   status = Status::Ok;
    bool                     finished = false;

    void line(const char* t) override { lines.emplace_back(t); }
    void kv(const char* k, const char* v) override { lines.emplace_back(std::string(k) + "=" + v); }
    void done(Status s) override { status = s; finished = true; }

    [[nodiscard]] bool contains(const char* needle) const {
        for (auto const& l : lines) {
            if (l.find(needle) != std::string::npos) return true;
        }
        return false;
    }
};

Status run(const char* cmdline, RecordingSink& sink) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s", cmdline);
    return cli::dispatch_line(buf, sink);
}

uint32_t g_fake_ms = 0;
uint32_t fake_millis() { return g_fake_ms; }

}  // namespace

void test_help_is_generated() {
    RecordingSink s;
    CHECK(run("help", s) == Status::Ok);
    CHECK(s.contains("sys"));
    CHECK(s.contains("help"));
    CHECK(s.finished);

    RecordingSink g;
    run("help sys", g);
    CHECK(g.contains("sys stat"));
    CHECK(g.contains("sys debug"));
    // help text comes from the same row the parser matches -- it cannot drift (D13)
    CHECK(g.contains("per-module log levels"));
}

void test_unknown_command_suggests() {
    RecordingSink s;
    CHECK(run("sys stst", s) == Status::BadArg);
    CHECK(s.contains("unknown command"));
    CHECK(s.contains("did you mean"));
    CHECK(s.contains("sys stat"));
}

void test_debug_command_sets_levels() {
    log::init(log::Level::Info);
    RecordingSink s;

    CHECK(run("sys debug motion verbose", s) == Status::Ok);
    CHECK(log::get(log::Mod::motion) == log::Level::Verbose);

    RecordingSink g;
    CHECK(run("sys debug drv.* debug", g) == Status::Ok);
    CHECK(log::get(log::Mod::drv_opto) == log::Level::Debug);
    CHECK(g.contains("10 modules"));

    RecordingSink p;
    CHECK(run("sys debug ui v", p) == Status::Ok);          // prefix level
    CHECK(log::get(log::Mod::ui) == log::Level::Verbose);

    RecordingSink bad;
    CHECK(run("sys debug ui banana", bad) == Status::BadArg);
    CHECK(bad.contains("bad level"));

    RecordingSink nomod;
    CHECK(run("sys debug nosuch info", nomod) == Status::BadArg);
    CHECK(nomod.contains("no module matches"));

    RecordingSink list;
    CHECK(run("sys debug", list) == Status::Ok);            // bare form lists the table
    CHECK(list.contains("motion"));
    CHECK(list.contains("ceiling"));
}

void test_aliases() {
    RecordingSink s;
    run("?", s);                                            // ? -> help
    CHECK(s.contains("groups"));
}

void test_unsafe_gate() {
    cli::set_millis_fn(fake_millis);
    g_fake_ms = 0;
    cli::unsafe_set(false);
    CHECK(!cli::unsafe_active());

    cli::unsafe_set(true);
    CHECK(cli::unsafe_active());

    g_fake_ms = 59'000;                 // still inside the 60 s window
    CHECK(cli::unsafe_active());

    g_fake_ms = 61'000;                 // expired
    CHECK(!cli::unsafe_active());
}

void test_quoted_args() {
    RecordingSink s;
    char buf[] = "help \"sys\"";
    cli::dispatch_line(buf, s);
    CHECK(s.contains("sys stat"));
}

void run_cli_tests() {
    test_help_is_generated();
    test_unknown_command_suggests();
    test_debug_command_sets_levels();
    test_aliases();
    test_unsafe_gate();
    test_quoted_args();
}
