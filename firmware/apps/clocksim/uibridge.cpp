#include "uibridge.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "clk/cli/host_dispatch.hpp"
#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"
#include "clk/hal/host/sim.hpp"
#include "clk/log.hpp"
#include "clk/services/chrono.hpp"
#include "clk/services/motion.hpp"
#include "clk/services/ui.hpp"

namespace clk::uibridge {
namespace {

namespace sim = hal::host;

constexpr uint32_t kFrameMs = 20;      // 50 Hz -- a hand slewing at full speed stays smooth
constexpr std::size_t kLogRing = 256;  // what a freshly-attached UI can still see
constexpr std::size_t kLogBacklog = 64;
constexpr std::size_t kMaxClients = 4;
constexpr auto kDispatchWait = std::chrono::milliseconds(250);

// ---- the log ring ------------------------------------------------------------------------
// The tap runs on whichever thread logged, so this is many-producer.  A mutex is the right
// answer here and not a compromise: clocksim's log rate is human-scale, and rule 12's actual
// requirement is that a slow reader never back-pressures the producer -- which is satisfied
// by overwriting the oldest entry and telling the reader how many it missed.
struct LogLine {
    log::Mod mod;
    log::Level lvl;
    char text[192];
};

std::mutex g_log_mx;
LogLine g_log[kLogRing];
uint64_t g_log_seq = 0;  // total ever written; the slot is seq % kLogRing

void log_tap(log::Mod m, log::Level l, const char* text) noexcept {
    std::lock_guard lk{g_log_mx};
    LogLine& s = g_log[g_log_seq % kLogRing];
    s.mod = m;
    s.lvl = l;
    std::snprintf(s.text, sizeof s.text, "%s", text);
    ++g_log_seq;
}

// ---- JSON --------------------------------------------------------------------------------
// Emit only.  Escapes what RFC 8259 requires plus DEL, and truncates rather than growing --
// a log line that does not fit is a log line that was already too long to read.
void json_str(const char* in, char* out, std::size_t cap) {
    if (cap < 3) {
        if (cap) out[0] = '\0';
        return;
    }
    std::size_t n = 0;
    out[n++] = '"';
    for (const char* p = in; *p && n + 8 < cap; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
            case '"':
                out[n++] = '\\';
                out[n++] = '"';
                break;
            case '\\':
                out[n++] = '\\';
                out[n++] = '\\';
                break;
            case '\n':
                out[n++] = '\\';
                out[n++] = 'n';
                break;
            case '\r':
                out[n++] = '\\';
                out[n++] = 'r';
                break;
            case '\t':
                out[n++] = '\\';
                out[n++] = 't';
                break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    n += static_cast<std::size_t>(std::snprintf(out + n, cap - n, "\\u%04x", c));
                } else {
                    out[n++] = static_cast<char>(c);
                }
        }
    }
    out[n++] = '"';
    out[n] = '\0';
}

// ---- sockets -------------------------------------------------------------------------------
// std::thread cannot be asked whether it has finished, so each client raises its own flag on
// the way out and the accept loop reaps the raised ones.  The unique_ptr is what makes the
// address stable across a vector reallocation while the thread still holds it.
struct Client {
    std::thread th;
    std::atomic<bool> done{false};
};

std::atomic<bool> g_run{false};
std::atomic<int> g_listen{-1};
std::atomic<uint16_t> g_port{0};
std::thread g_accept_thr;
std::mutex g_clients_mx;
std::vector<std::unique_ptr<Client>> g_clients;

bool send_all(int fd, const char* p, std::size_t n) {
    while (n) {
        const ssize_t w = ::send(fd, p, n, 0);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

bool send_line(int fd, const char* s) {
    return send_all(fd, s, std::strlen(s)) && send_all(fd, "\n", 1);
}

// ---- frames --------------------------------------------------------------------------------

bool send_hello(int fd) {
    const auto& b = cli::build_info();
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "{\"t\":\"hello\",\"proto\":%d,\"app\":\"clock-sim %s\",\"board\":\"%s\","
                  "\"profile\":\"%s\",\"usteps_per_rev\":%" PRId32
                  ",\"pixels\":%zu,"
                  "\"px_names\":[\"dial0\",\"dial1\",\"bell\",\"alarm\",\"clock\",\"vol\","
                  "\"batt\"]}",
                  kProtoVersion, b.app_version, b.board, b.profile, hal::motor::kUstepsPerRev,
                  hal::pixels::kCount);
    return send_line(fd, buf);
}

bool send_state(int fd) {
    const auto s = sim::snapshot();
    char px[8 * 24];
    std::size_t n = 0;
    for (std::size_t i = 0; i < hal::pixels::kCount; ++i) {
        n += static_cast<std::size_t>(std::snprintf(px + n, sizeof px - n, "%s[%u,%u,%u,%u]",
                                                    i ? "," : "", s.px[i].r, s.px[i].g, s.px[i].b,
                                                    s.px[i].w));
    }

    // The AOs' own view, alongside the hardware's.  These are read-only snapshots; the
    // bridge never posts to a service except through a CLI line, like everyone else.
    const auto mo = svc::motion().snapshot();
    const auto uo = svc::ui().snapshot();
    const auto ch = svc::chrono().snapshot();

    char buf[2048];
    std::snprintf(
        buf, sizeof buf,
        "{\"t\":\"state\",\"ms\":%" PRIu64
        ",\"warp\":%.3f,"
        "\"hands\":{\"h\":%.3f,\"m\":%.3f,\"hp\":%" PRId32 ",\"mp\":%" PRId32 ",\"hv\":%" PRId32
        ",\"mv\":%" PRId32
        ",\"moving\":%s,\"motor\":%s},"
        "\"motion\":{\"state\":\"%s\",\"phase\":\"%s\",\"homed\":%s,\"th\":%" PRId32
        ",\"tm\":%" PRId32 ",\"home_ms\":%" PRIu32 ",\"faults\":%" PRIu32
        "},"
        "\"ui\":{\"mode\":\"%s\",\"armed\":%s,\"alarm_h\":%d,\"alarm_m\":%d,\"vol\":%u,"
        "\"idle_in\":%u},"
        "\"clock\":{\"h\":%d,\"m\":%d,\"s\":%d,\"valid\":%s,\"follow\":%s},"
        "\"px\":[%s],\"refreshed\":%s,"
        "\"wake\":{\"warm\":%u,\"cool\":%u},"
        "\"spk\":{\"on\":%s,\"vol\":%u},"
        "\"pwr\":{\"plugged\":%s,\"mv\":%u,\"soc\":%u,\"chrg\":%s},"
        "\"knob\":{\"count\":%" PRId32
        ",\"sw\":%s},"
        "\"opto\":{\"n\":%.4f,\"auto\":%s},"
        "\"imu\":{\"yaw\":%.2f,\"taps\":%u},"
        "\"radio_off\":%s}",
        s.sim_us / 1000u, s.warp, static_cast<double>(s.hand_deg[0]),
        static_cast<double>(s.hand_deg[1]), s.hand_pos[0], s.hand_pos[1], s.hand_vel[0],
        s.hand_vel[1], (s.hand_moving[0] || s.hand_moving[1]) ? "true" : "false",
        s.motor_on ? "true" : "false", mo.state_name, mo.phase, mo.homed ? "true" : "false",
        mo.target_hour, mo.target_minute, mo.home_ms, mo.faults, uo.mode_name,
        uo.alarm_armed ? "true" : "false", uo.alarm_hour, uo.alarm_minute, uo.volume, uo.idle_in_ms,
        ch.hour, ch.minute, ch.second, ch.valid ? "true" : "false", ch.follow ? "true" : "false",
        px, s.refreshed ? "true" : "false", s.warm_pct, s.cool_pct, s.spk_active ? "true" : "false",
        s.vol_pct, s.plugged ? "true" : "false", s.vbat_mv, s.soc_pct,
        s.charging ? "true" : "false", s.knob_count, s.knob_sw ? "true" : "false",
        static_cast<double>(s.opto), s.opto_auto ? "true" : "false", static_cast<double>(s.yaw_deg),
        s.taps, s.radio_off ? "true" : "false");
    return send_line(fd, buf);
}

// Emits everything written since `cursor`, and says so when the ring lapped.
bool drain_logs(int fd, uint64_t& cursor) {
    for (;;) {
        LogLine line{};
        uint64_t skipped = 0;
        {
            std::lock_guard lk{g_log_mx};
            if (cursor >= g_log_seq) return true;
            if (g_log_seq - cursor > kLogRing) {
                skipped = g_log_seq - cursor - kLogRing;
                cursor = g_log_seq - kLogRing;
            }
            line = g_log[cursor % kLogRing];
            ++cursor;
        }
        if (skipped) {
            char note[112];
            std::snprintf(note, sizeof note,
                          "{\"t\":\"log\",\"lvl\":\"warn\",\"mod\":\"cli\",\"msg\":\"... %" PRIu64
                          " log lines dropped\"}",
                          skipped);
            if (!send_line(fd, note)) return false;
        }
        char txt[sizeof(line.text) * 2 + 8];
        json_str(line.text, txt, sizeof txt);
        char buf[sizeof(txt) + 128];
        std::snprintf(buf, sizeof buf, "{\"t\":\"log\",\"lvl\":\"%s\",\"mod\":\"%s\",\"msg\":%s}",
                      log::name(line.lvl), log::name(line.mod), txt);
        if (!send_line(fd, buf)) return false;
    }
}

// ---- inbound -------------------------------------------------------------------------------

// Collects a command's output as pre-escaped JSON array elements.
class JsonSink final : public cmd::Sink {
public:
    std::string arr;
    Status st = Status::Ok;

    void line(const char* t) override { push(t); }
    void kv(const char* k, const char* v) override {
        char b[256];
        std::snprintf(b, sizeof b, "%s=%s", k, v);
        push(b);
    }
    void done(Status s) override { st = s; }

private:
    void push(const char* t) {
        char esc[768];
        json_str(t, esc, sizeof esc);
        if (!arr.empty()) arr += ',';
        arr += esc;
    }
};

void handle_line(int fd, char* line) {
    long id = -1;
    char* cmdline = line;
    if (*cmdline == '#') {
        char* end = nullptr;
        const long v = std::strtol(cmdline + 1, &end, 10);
        if (end && end != cmdline + 1) {
            id = v;
            cmdline = end;
            while (*cmdline == ' ') ++cmdline;
        }
    }
    if (!*cmdline) return;

    JsonSink sink;
    {
        // A `sensor ... stream` on stdin owns the console for up to 120 s.  Waiting that
        // long would stall this client's state frames, so give up and say Busy -- which is
        // the truth, and which the UI can show.
        std::unique_lock lk{cli::host::dispatch_mutex(), std::defer_lock};
        if (!lk.try_lock_for(kDispatchWait)) {
            sink.st = Status::Busy;
        } else {
            cli::dispatch_line(cmdline, sink);
        }
    }

    char buf[4096];
    if (id >= 0) {
        std::snprintf(buf, sizeof buf, "{\"t\":\"res\",\"id\":%ld,\"st\":\"%s\",\"lines\":[%s]}",
                      id, cmd::name(sink.st), sink.arr.c_str());
    } else {
        std::snprintf(buf, sizeof buf, "{\"t\":\"res\",\"st\":\"%s\",\"lines\":[%s]}",
                      cmd::name(sink.st), sink.arr.c_str());
    }
    send_line(fd, buf);
}

// ---- the client loop -------------------------------------------------------------------------

void client_loop(int fd, Client* self) {
    const int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    uint64_t cursor = 0;
    {
        std::lock_guard lk{g_log_mx};
        cursor = g_log_seq > kLogBacklog ? g_log_seq - kLogBacklog : 0;
    }

    if (!send_hello(fd) || !send_state(fd)) {
        ::close(fd);
        self->done.store(true, std::memory_order_release);
        return;
    }
    CLK_LOGI(sim, "ui: client attached");

    char acc[1024];
    std::size_t acc_n = 0;
    auto next_frame = std::chrono::steady_clock::now();

    while (g_run.load(std::memory_order_relaxed)) {
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, 5);
        if (r > 0 && (p.revents & (POLLHUP | POLLERR | POLLNVAL))) break;
        if (r > 0 && (p.revents & POLLIN)) {
            char tmp[512];
            const ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; ++i) {
                const char c = tmp[i];
                if (c == '\n' || c == '\r') {
                    acc[acc_n] = '\0';
                    if (acc_n) handle_line(fd, acc);
                    acc_n = 0;
                } else if (acc_n + 1 < sizeof acc) {
                    acc[acc_n++] = c;
                }
                // An over-long line is clipped rather than split into two commands: half a
                // command is worse than none.
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= next_frame) {
            if (!send_state(fd)) break;
            next_frame = now + std::chrono::milliseconds(kFrameMs);
        }
        if (!drain_logs(fd, cursor)) break;
    }

    CLK_LOGI(sim, "ui: client gone");
    ::close(fd);
    self->done.store(true, std::memory_order_release);
}

void accept_loop() {
    while (g_run.load(std::memory_order_relaxed)) {
        const int ls = g_listen.load(std::memory_order_relaxed);
        if (ls < 0) break;
        pollfd p{ls, POLLIN, 0};
        if (::poll(&p, 1, 100) <= 0) continue;
        const int fd = ::accept(ls, nullptr, nullptr);
        if (fd < 0) continue;
        if (!g_run.load(std::memory_order_relaxed)) {
            ::close(fd);
            break;
        }
        std::lock_guard lk{g_clients_mx};
        // Reap the finished ones first, so a page reloaded fifty times does not accumulate
        // fifty joinable threads.
        for (auto it = g_clients.begin(); it != g_clients.end();) {
            if ((*it)->done.load(std::memory_order_acquire)) {
                if ((*it)->th.joinable()) (*it)->th.join();
                it = g_clients.erase(it);
            } else {
                ++it;
            }
        }
        if (g_clients.size() >= kMaxClients) {
            ::close(fd);
            CLK_LOGW(sys, "ui: refused a client -- %zu already attached", kMaxClients);
            continue;
        }
        auto& c = g_clients.emplace_back(std::make_unique<Client>());
        c->th = std::thread(client_loop, fd, c.get());
    }
}

}  // namespace

Status start(uint16_t p) noexcept {
    if (p == 0) return Status::Ok;  // explicitly disabled
    if (g_run.load()) return Status::Busy;

    const int ls = ::socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) {
        CLK_LOGW(sys, "ui: socket() failed -- no UI bridge");
        return Status::Failed;
    }
    const int one = 1;
    ::setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(p);
    // Loopback only, deliberately: this surface runs arbitrary CLI commands, and the CLI can
    // move hardware.  It has no business being reachable from the network.
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(ls, 4) != 0) {
        ::close(ls);
        CLK_LOGW(sys, "ui: port %u is taken -- console only (--ui-port to move it)", p);
        return Status::Failed;
    }

    g_listen.store(ls);
    g_port.store(p);
    g_run.store(true);
    log::set_tap(log_tap);
    g_accept_thr = std::thread(accept_loop);
    CLK_LOGI(sys, "ui: bridge on 127.0.0.1:%u", p);
    return Status::Ok;
}

void stop() noexcept {
    if (!g_run.exchange(false)) return;
    log::set_tap(nullptr);
    const int ls = g_listen.exchange(-1);
    if (ls >= 0) {
        ::shutdown(ls, SHUT_RDWR);
        ::close(ls);
    }
    if (g_accept_thr.joinable()) g_accept_thr.join();
    std::lock_guard lk{g_clients_mx};
    for (auto& c : g_clients) {
        if (c->th.joinable()) c->th.join();
    }
    g_clients.clear();
    g_port.store(0);
}

bool running() noexcept { return g_run.load(); }
uint16_t port() noexcept { return g_port.load(); }

}  // namespace clk::uibridge
