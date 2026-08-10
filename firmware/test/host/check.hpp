// Minimal test harness, stand-in until vendor/googletest is initialised.  [§11.1]
#pragma once
#include <cstdio>
#include <cstring>

inline int g_checks = 0;
inline int g_fails  = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_fails;                                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                             \
    } while (0)

#define CHECK_STREQ(a, b)                                                             \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (std::strcmp((a), (b)) != 0) {                                             \
            ++g_fails;                                                                \
            std::printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        }                                                                             \
    } while (0)

inline int check_summary(const char* what) {
    std::printf("%s: %d checks, %d failures\n", what, g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
