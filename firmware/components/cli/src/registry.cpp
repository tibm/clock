#include "clk/cli/registry.hpp"

#include <cstdio>
#include <cstring>

#include "clk/log.hpp"

namespace clk::cli {
// ---- the tables ----------------------------------------------------------------------
// Each group's rows live in their own .cpp and are collected here.  Adding a group is one
// extern + one row in kAllTables; there is no registration call anywhere else (rule 10).
extern const CmdTable kTableSys;
extern const CmdTable kTableTop;   // help / unsafe

namespace {

const CmdTable* const kAllTables[] = { &kTableSys, &kTableTop };

// ---- aliases -------------------------------------------------------------------------
// `hand goto 7:15` -> `motion goto 7:15`.  Typing cost stays low without giving up the
// one-vocabulary rule (D13).  Multi-token expansions are allowed: `led 3 red` -> `ui led 3 red`.
struct Alias { const char* from; const char* to[2]; };
constexpr Alias kAliases[] = {
    { "hand", { "motion", nullptr } },
    { "snd",  { "audio",  nullptr } },
    { "fs",   { "storage", nullptr } },
    { "led",  { "ui",     "led"   } },
    { "time", { "chrono", "time"  } },
    { "i2c",  { "board",  "i2c"   } },
    { "?",    { "help",   nullptr } },
};

constexpr int kMaxArgs = 16;

bool eq(const char* a, const char* b) noexcept { return a && b && std::strcmp(a, b) == 0; }

uint32_t millis_stub() noexcept { return 0; }
MillisFn g_millis = millis_stub;

bool     g_unsafe_on      = false;
uint32_t g_unsafe_last_ms = 0;
constexpr uint32_t kUnsafeWindowMs = 60u * 1000u;

// Levenshtein, capped -- used only to say "did you mean" on a miss.
int edit_distance(std::string_view a, std::string_view b) noexcept {
    constexpr std::size_t kMax = 24;
    if (a.size() > kMax || b.size() > kMax) return 99;
    int prev[kMax + 1], cur[kMax + 1];
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int m = prev[j] + 1;
            if (cur[j - 1] + 1 < m) m = cur[j - 1] + 1;
            if (prev[j - 1] + cost < m) m = prev[j - 1] + cost;
            cur[j] = m;
        }
        for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = cur[j];
    }
    return prev[b.size()];
}

void suggest(Sink& out, const char* const* argv, int argc) noexcept {
    char want[64];
    std::snprintf(want, sizeof want, "%s%s%s", argv[0],
                  argc > 1 ? " " : "", argc > 1 ? argv[1] : "");

    const CmdSpec* best[3] = {};
    int            bestd[3] = { 99, 99, 99 };

    std::size_t nt = 0;
    const CmdTable* const* t = &kAllTables[0];
    nt = sizeof(kAllTables) / sizeof(kAllTables[0]);
    for (std::size_t ti = 0; ti < nt; ++ti) {
        for (std::size_t ri = 0; ri < t[ti]->count; ++ri) {
            const CmdSpec& r = t[ti]->rows[ri];
            char full[64];
            std::snprintf(full, sizeof full, "%s %s%s%s", r.group,
                          r.object ? r.object : "", r.object ? " " : "", r.verb);
            const int d = edit_distance(want, full);
            for (int k = 0; k < 3; ++k) {
                if (d < bestd[k]) {
                    for (int m = 2; m > k; --m) { bestd[m] = bestd[m - 1]; best[m] = best[m - 1]; }
                    bestd[k] = d; best[k] = &r;
                    break;
                }
            }
        }
    }
    out.printf("unknown command: %s", want);
    for (int k = 0; k < 3; ++k) {
        if (best[k] && bestd[k] <= 6) {
            out.printf("  did you mean:  %s %s%s%s", best[k]->group,
                       best[k]->object ? best[k]->object : "",
                       best[k]->object ? " " : "", best[k]->verb);
        }
    }
    out.line("  `help` lists the groups");
}

}  // namespace

// ---- public ---------------------------------------------------------------------------

void set_millis_fn(MillisFn f) noexcept { if (f) g_millis = f; }

void unsafe_set(bool on) noexcept {
    g_unsafe_on = on;
    g_unsafe_last_ms = g_millis();
}

bool unsafe_active() noexcept {
    if (!g_unsafe_on) return false;
    if (g_millis() - g_unsafe_last_ms > kUnsafeWindowMs) {
        g_unsafe_on = false;
        return false;
    }
    return true;
}

const CmdSpec* find(int argc, const char* const* argv, int& first) noexcept {
    if (argc < 1) return nullptr;
    const std::size_t nt = sizeof(kAllTables) / sizeof(kAllTables[0]);

    // Longest match first: group+object+verb (3 tokens) beats group+verb (2).
    for (int want = 3; want >= 2; --want) {
        if (argc < want) continue;
        for (std::size_t ti = 0; ti < nt; ++ti) {
            for (std::size_t ri = 0; ri < kAllTables[ti]->count; ++ri) {
                const CmdSpec& r = kAllTables[ti]->rows[ri];
                if (!eq(r.group, argv[0])) continue;
                if (want == 3) {
                    if (r.object && eq(r.object, argv[1]) && eq(r.verb, argv[2])) {
                        first = 3; return &r;
                    }
                } else {
                    if (!r.object && eq(r.verb, argv[1])) { first = 2; return &r; }
                }
            }
        }
    }
    // A group with a bare verb-less form, e.g. `help` or `sys debug` with no args.
    for (std::size_t ti = 0; ti < nt; ++ti) {
        for (std::size_t ri = 0; ri < kAllTables[ti]->count; ++ri) {
            const CmdSpec& r = kAllTables[ti]->rows[ri];
            if (eq(r.group, argv[0]) && !r.object && eq(r.verb, "")) { first = 1; return &r; }
        }
    }
    return nullptr;
}

void help(Sink& out, const char* group, const char* verb) noexcept {
    const std::size_t nt = sizeof(kAllTables) / sizeof(kAllTables[0]);
    const auto& b = build_info();

    if (!group) {
        char buf[160] = "groups  ";
        for (std::size_t ti = 0; ti < nt; ++ti) {
            const char* last = nullptr;
            for (std::size_t ri = 0; ri < kAllTables[ti]->count; ++ri) {
                const char* g = kAllTables[ti]->rows[ri].group;
                if (last && eq(last, g)) continue;
                last = g;
                std::strncat(buf, g, sizeof buf - std::strlen(buf) - 1);
                std::strncat(buf, "  ", sizeof buf - std::strlen(buf) - 1);
            }
        }
        out.line(buf);
        out.line("        help [<group> [<verb>]]      unsafe <on|off>");
        out.printf("        profile=%s  board=%s  unsafe=%s",
                   b.profile, b.board, unsafe_active() ? "ON" : "OFF");
        return;
    }

    bool any = false;
    for (std::size_t ti = 0; ti < nt; ++ti) {
        for (std::size_t ri = 0; ri < kAllTables[ti]->count; ++ri) {
            const CmdSpec& r = kAllTables[ti]->rows[ri];
            if (!eq(r.group, group)) continue;
            if (verb && !eq(r.verb, verb) && !(r.object && eq(r.object, verb))) continue;
            any = true;
            char line[128];
            std::snprintf(line, sizeof line, "%s %s%s%s %s", r.group,
                          r.object ? r.object : "", r.object ? " " : "",
                          r.verb, r.args ? r.args : "");
            // pad to a column so the help text lines up
            const int pad = 34 - static_cast<int>(std::strlen(line));
            out.printf("%s%*s%s%s", line, pad > 1 ? pad : 1, "", r.help,
                       (r.flags & Unsafe) ? "   [unsafe]" : "");
        }
    }
    if (!any) out.printf("no such group: %s   (`help` lists them)", group);
}

Status dispatch(int argc, const char* const* argv, Sink& out) noexcept {
    if (argc < 1) { out.done(Status::Ok); return Status::Ok; }

    // Alias expansion, one level.
    const char* expanded[kMaxArgs];
    for (const auto& a : kAliases) {
        if (!eq(a.from, argv[0])) continue;
        int n = 0;
        expanded[n++] = a.to[0];
        if (a.to[1]) expanded[n++] = a.to[1];
        for (int i = 1; i < argc && n < kMaxArgs; ++i) expanded[n++] = argv[i];
        argv = expanded;
        argc = n;
        break;
    }

    int first = 0;
    const CmdSpec* spec = find(argc, argv, first);
    if (!spec) {
        suggest(out, argv, argc);
        out.done(Status::BadArg);
        return Status::BadArg;
    }

    if ((spec->flags & Unsafe) && !unsafe_active()) {
        out.printf("denied: `%s %s` needs `unsafe on` (expires 60 s after the last use)",
                   spec->group, spec->verb);
        out.done(Status::Denied);
        return Status::Denied;
    }
    if (spec->flags & Unsafe) unsafe_set(true);   // sliding window

    Args args{ argc, argv, first };
    const Status st = spec->run(args, out);
    out.done(st);
    return st;
}

Status dispatch_line(char* line, Sink& out) noexcept {
    const char* argv[kMaxArgs];
    int argc = 0;
    char* p = line;
    while (*p && argc < kMaxArgs) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        char quote = 0;
        if (*p == '"' || *p == '\'') { quote = *p; ++p; }
        argv[argc++] = p;
        while (*p && (quote ? *p != quote : (*p != ' ' && *p != '\t'))) ++p;
        if (*p) *p++ = '\0';
    }
    if (argc == 0) return Status::Ok;
    return dispatch(argc, argv, out);
}

}  // namespace clk::cli
