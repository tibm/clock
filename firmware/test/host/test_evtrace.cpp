// The event tracer's ring: order, wrap, repeats, the boot split, and the `sys` field
// diagnostics that read it.                                       [FIRMWARE.md §6.9, §9.3]
//
// Runs before any AO is up, so this thread is the ring's only writer.
#include <vector>

#include "check.hpp"
#include "testutil.hpp"

#include "clk/evtrace.hpp"

using namespace clk;

namespace {

std::vector<evtrace::Entry> all() {
    std::vector<evtrace::Entry> v(evtrace::kEntries);
    v.resize(evtrace::snapshot(v.data(), v.size()));
    return v;
}

HandTarget target(int32_t h) { return HandTarget{h, 0, false}; }

void test_evtrace_records_in_order_with_payload() {
    evtrace::clear();
    const uint8_t src = evtrace::source("test");
    CHECK(src != 0xFF);
    CHECK(evtrace::source("test") == src);  // same name, same label
    evtrace::record(src, target(0x01020304));
    evtrace::record(src, Tap{});
    const auto v = all();
    CHECK(v.size() == 2);
    CHECK(v[0].seq + 1 == v[1].seq);
    CHECK(std::string(evtrace::tag_name(v[0].tag)) == "HandTarget");
    CHECK(std::string(evtrace::source_name(v[0].src)) == "test");
    CHECK(v[0].data[0] == 0x04 && v[0].data[3] == 0x01);  // the first field's bytes
    CHECK(std::string(evtrace::tag_name(v[1].tag)) == "Tap");
    CHECK(v[1].data[0] == 0 && !v[1].prev);
}

void test_evtrace_keeps_the_newest_when_it_wraps() {
    evtrace::clear();
    const uint8_t src = evtrace::source("test");
    for (int32_t k = 0; k < 300; ++k) evtrace::record(src, target(k));
    const auto v = all();
    CHECK(v.size() == evtrace::kEntries);
    bool contiguous = true;
    for (std::size_t k = 1; k < v.size(); ++k) contiguous &= v[k].seq == v[k - 1].seq + 1;
    CHECK(contiguous);
    CHECK(v.back().data[0] == 299 % 256 && v.back().data[1] == 299 / 256);
    CHECK(v.front().data[0] == 300 - 256);
}

// The dial tick is re-posted every 500 ms unchanged; it must not flush the ring.
void test_evtrace_counts_repeats_instead_of_storing_them() {
    evtrace::clear();
    const uint8_t src = evtrace::source("test");
    const uint32_t rep0 = evtrace::stats().repeats;
    for (int k = 0; k < 10; ++k) evtrace::record(src, DialTick{3});
    evtrace::record(src, DialTick{4});
    evtrace::record(src, DialTick{3});  // a change back is news again
    CHECK(all().size() == 3);
    CHECK(evtrace::stats().repeats - rep0 == 9);
}

// A warm reset: what the ring held belongs to the previous boot and is kept, marked.
void test_evtrace_carries_the_previous_boot() {
    evtrace::clear();
    const uint8_t src = evtrace::source("test");
    evtrace::record(src, target(1));
    evtrace::record(src, target(2));
    evtrace::init();
    CHECK(evtrace::stats().warm);
    CHECK(evtrace::stats().carried == 2);
    CHECK(evtrace::source("test") == src);  // labels survive too
    evtrace::record(src, target(3));
    const auto v = all();
    CHECK(v.size() == 3);
    CHECK(v[0].prev && v[1].prev && !v[2].prev);

    RecordingSink s;
    CHECK(run("sys ev dump", s) == Status::Ok);
    CHECK(s.contains("-- previous boot --"));
    CHECK(s.contains("-- this boot"));
    CHECK(s.contains("HandTarget"));
    RecordingSink s2;
    CHECK(run("sys ev dump 1", s2) == Status::Ok);
    CHECK(!s2.contains("previous boot"));  // only the newest, which is this boot's

    RecordingSink c;
    CHECK(run("sys ev clear", c) == Status::Ok);
    CHECK(all().empty());
    CHECK(evtrace::stats().carried == 0);
}

// Where there is no chip to ask, the field diagnostics say so rather than invent numbers.
void test_sys_diagnostics_on_the_host() {
    RecordingSink h, t, c, bad;
    CHECK(run("sys heap", h) == Status::NotPresent);
    CHECK(h.contains("host"));
    CHECK(run("sys top", t) == Status::NotPresent);
    CHECK(run("sys coredump info", c) == Status::NotPresent);
    CHECK(run("sys top 7", bad) == Status::BadArg);  // under the 100 ms floor
}

}  // namespace

void run_evtrace_tests() {
    test_evtrace_records_in_order_with_payload();
    test_evtrace_keeps_the_newest_when_it_wraps();
    test_evtrace_counts_repeats_instead_of_storing_them();
    test_evtrace_carries_the_previous_boot();
    test_sys_diagnostics_on_the_host();
    evtrace::clear();
}
