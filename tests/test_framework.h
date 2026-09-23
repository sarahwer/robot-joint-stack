/*
 * test_framework.h - minimal unit test helpers (no external dependency).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_TEST_FRAMEWORK_H
#define RJS_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>

static int g_failures = 0;
static int g_checks   = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!(cond)) {                                                              \
            g_failures++;                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        long long _a = (long long)(a), _b = (long long)(b);                         \
        g_checks++;                                                                 \
        if (_a != _b) {                                                             \
            g_failures++;                                                           \
            printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__,   \
                   #a, #b, _a, _b);                                                 \
        }                                                                           \
    } while (0)

#define RUN(test_fn)                                                                \
    do {                                                                            \
        printf("[ RUN ] %s\n", #test_fn);                                           \
        test_fn();                                                                  \
    } while (0)

#define TEST_MAIN_END()                                                             \
    do {                                                                            \
        printf("%d checks, %d failures\n", g_checks, g_failures);                   \
        return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;                       \
    } while (0)

#endif /* RJS_TEST_FRAMEWORK_H */
