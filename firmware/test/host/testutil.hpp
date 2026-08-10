// Shared test helpers.                                     [FIRMWARE.md §11.1]
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "clk/cli/registry.hpp"

// A Sink that records instead of printing.  This is the seam that makes every CLI command
// assertable without a console -- the same one BLE will use to pack a notification.
class RecordingSink final : public clk::cmd::Sink {
public:
    std::vector<std::string> lines;
    clk::Status status = clk::Status::Ok;
    bool finished = false;

    void line(const char* t) override { lines.emplace_back(t); }
    void kv(const char* k, const char* v) override { lines.emplace_back(std::string(k) + "=" + v); }
    void done(clk::Status s) override {
        status = s;
        finished = true;
    }

    [[nodiscard]] bool contains(const char* needle) const {
        for (auto const& l : lines) {
            if (l.find(needle) != std::string::npos) return true;
        }
        return false;
    }
    [[nodiscard]] std::string joined() const {
        std::string s;
        for (auto const& l : lines) {
            s += l;
            s += '\n';
        }
        return s;
    }
};

inline clk::Status run(const char* cmdline, RecordingSink& sink) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s", cmdline);
    return clk::cli::dispatch_line(buf, sink);
}
