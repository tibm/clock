#include "clk/transport/frame.hpp"

#include <cstdio>
#include <cstring>

namespace clk::transport {

bool parse_request(const uint8_t* data, std::size_t len, Request& out) noexcept {
    out = Request{};
    if (!data) return false;
    // A terminal app appends CR/LF, and a C-string writer may send its NUL.  Neither is part
    // of the command.
    while (len && (data[len - 1] == '\n' || data[len - 1] == '\r' || data[len - 1] == 0)) --len;
    if (len == 0) return false;
    if (std::memchr(data, 0, len)) return false;

    std::size_t i = 0;
    uint32_t id = 0;
    while (i < len && data[i] >= '0' && data[i] <= '9' && i < 6) {
        id = id * 10 + static_cast<uint32_t>(data[i] - '0');
        ++i;
    }
    // Digits followed by a space are an id.  Anything else -- "sys ver", or a bare "42" --
    // is the line itself, with id 0.
    if (i > 0 && i < len && data[i] == ' ') {
        if (id > 0xFFFF) return false;
        out.id = static_cast<uint16_t>(id);
        ++i;
        while (i < len && data[i] == ' ') ++i;
    } else {
        i = 0;
    }
    while (i < len && (data[i] == ' ' || data[i] == '\t')) ++i;  // blank is not a command
    const std::size_t n = len - i;
    if (n == 0 || n > kMaxLine) return false;
    std::memcpy(out.line, data + i, n);
    out.line[n] = '\0';
    return true;
}

std::size_t emit_record(uint16_t id, Kind kind, const char* text, std::size_t max_frame,
                        EmitFn emit, void* ctx) noexcept {
    if (!emit) return 0;
    if (!text) text = "";
    char head[8];
    const int hn = std::snprintf(head, sizeof head, "%u", static_cast<unsigned>(id));
    // The header plus the kind char plus at least one byte of text, or nothing can progress.
    if (max_frame < static_cast<std::size_t>(hn) + 2) return 0;
    const std::size_t room = max_frame - static_cast<std::size_t>(hn) - 1;

    uint8_t buf[kMaxLine + 16];
    std::size_t left = std::strlen(text);
    std::size_t frames = 0;
    do {
        const bool last = left <= room;
        const std::size_t n = last ? left : room;
        const std::size_t cap = sizeof buf - static_cast<std::size_t>(hn) - 1;
        const std::size_t take = n < cap ? n : cap;  // max_frame beyond the buffer: clamp
        std::memcpy(buf, head, static_cast<std::size_t>(hn));
        buf[hn] = static_cast<uint8_t>(last && take == n ? static_cast<char>(kind) : '+');
        std::memcpy(buf + hn + 1, text, take);
        if (!emit(ctx, buf, static_cast<std::size_t>(hn) + 1 + take)) return frames;
        ++frames;
        text += take;
        left -= take;
    } while (left > 0);
    return frames;
}

}  // namespace clk::transport
