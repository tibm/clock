// The debug journal's ring: order, whole-line drops, the boot split, ANSI.   [FIRMWARE.md §9.4a]
//
// Pure: runs before any AO is up, so the only writer is this thread (and the log lines of
// whatever ran before, which the first drain throws away).
#include <cstdarg>
#include <cstring>
#include <string>

#include "check.hpp"

#include "clk/journal.hpp"

using namespace clk;

namespace {

void drain_all() {
    char buf[4096];
    for (;;) {
        const auto c = journal::peek(buf, sizeof buf);
        if (!c.n) return;
        journal::consume(c);
    }
}

// Everything waiting, as the consumer would see it, split by boot.
struct Seen {
    std::string prev, cur;
    bool mixed = false;
};
Seen take_all() {
    Seen s;
    char buf[1000];  // small on purpose: several chunks, and a chunk edge mid-line
    for (;;) {
        const auto c = journal::peek(buf, sizeof buf);
        if (!c.n) return s;
        if (c.prev && !s.cur.empty()) s.mixed = true;  // a previous-boot byte after a current one
        (c.prev ? s.prev : s.cur).append(buf, c.n);
        journal::consume(c);
    }
}

void put(const char* line) { journal::write(line, std::strlen(line)); }

void vput(const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    journal::vwrite(fmt, ap);
    va_end(ap);
}

void test_journal_in_order() {
    drain_all();
    put("one\n");
    put("two\n");
    const auto s = take_all();
    CHECK(s.cur == "one\ntwo\n");
    CHECK(s.prev.empty());
    CHECK(journal::stats().used == 0);
}

// Full: the OLDEST lines go, whole, and are counted.  What is left starts on a line.
void test_journal_drops_whole_oldest_lines() {
    drain_all();
    const uint32_t lost0 = journal::stats().lost;
    char line[64];
    const int kLines = 1000;  // ~24 KB into a 16 KB ring
    for (int i = 0; i < kLines; ++i) {
        std::snprintf(line, sizeof line, "line %05d xxxxxxxxxx\n", i);
        put(line);
    }
    const auto st = journal::stats();
    CHECK(st.used <= journal::kBytes);
    CHECK(st.lost > lost0);
    const auto s = take_all();
    CHECK(s.cur.rfind("line ", 0) == 0);                                // begins on a whole line
    CHECK(s.cur.find("line 00999 xxxxxxxxxx\n") != std::string::npos);  // the newest is kept
    CHECK(s.cur.find("line 00000 ") == std::string::npos);              // the oldest is not
    CHECK(s.cur.size() + (st.lost - lost0) == static_cast<std::size_t>(kLines) * 22u);
}

// A warm reset: what was waiting belongs to the previous boot, and comes out first and apart.
void test_journal_carries_the_previous_boot() {
    drain_all();
    put("before the reset\n");
    put("the last words\n");
    journal::init();
    const auto st = journal::stats();
    CHECK(st.warm);
    CHECK(st.carried == std::strlen("before the reset\nthe last words\n"));
    put("new boot\n");
    const auto s = take_all();
    CHECK(s.prev == "before the reset\nthe last words\n");
    CHECK(s.cur == "new boot\n");
    CHECK(!s.mixed);
}

// A consumer that looked, then got overtaken by a full ring, must not move the tail back.
void test_journal_consume_after_overrun() {
    drain_all();
    put("stale\n");
    char buf[64];
    const auto c = journal::peek(buf, sizeof buf);
    CHECK(c.n == 6);
    char line[64];
    for (int i = 0; i < 1000; ++i) {
        std::snprintf(line, sizeof line, "overrun %05d yyyyyyyy\n", i);
        put(line);
    }
    journal::consume(c);  // stale by now: a no-op
    const auto s = take_all();
    CHECK(s.cur.find("overrun 00999") != std::string::npos);
    CHECK(s.cur.rfind("overrun ", 0) == 0);
}

void test_journal_strips_colour_and_cuts_long_lines() {
    drain_all();
    vput("\033[0;33mW (%d) net: %s\033[0m\n", 12, "warned");
    std::string big(600, 'z');
    vput("%s\n", big.c_str());
    const auto s = take_all();
    CHECK(s.cur.rfind("W (12) net: warned\n", 0) == 0);
    const auto tail = s.cur.substr(std::strlen("W (12) net: warned\n"));
    CHECK(tail.size() == journal::kLineMax - 1);
    CHECK(!tail.empty() && tail.back() == '\n');
}

}  // namespace

void run_journal_tests() {
    test_journal_in_order();
    test_journal_drops_whole_oldest_lines();
    test_journal_carries_the_previous_boot();
    test_journal_consume_after_overrun();
    test_journal_strips_colour_and_cuts_long_lines();
    drain_all();
}
