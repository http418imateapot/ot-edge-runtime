/**
 * @file  test_helpers.h
 * @brief Minimal test framework for robust-binary-config tests.
 *
 * Intentionally zero-dependency (no external test frameworks) so it
 * works on bare-bones embedded Linux toolchains.
 */
#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Internal counters                                                   */
/* ------------------------------------------------------------------ */

static int _pass_count = 0;
static int _fail_count = 0;

/* ------------------------------------------------------------------ */
/* Assertion macros                                                    */
/* ------------------------------------------------------------------ */

/** Assert a boolean condition; continue on failure. */
#define TEST_ASSERT(cond) do { \
    if (cond) { \
        _pass_count++; \
    } else { \
        fprintf(stderr, "  FAIL  %s:%d  " #cond "\n", __FILE__, __LINE__); \
        _fail_count++; \
    } \
} while (0)

/** Assert equality between two integer-like values. */
#define TEST_ASSERT_EQ(a, b) do { \
    if ((a) == (b)) { \
        _pass_count++; \
    } else { \
        fprintf(stderr, "  FAIL  %s:%d  " #a " == " #b \
                "  (got %lld, expected %lld)\n", \
                __FILE__, __LINE__, (long long)(a), (long long)(b)); \
        _fail_count++; \
    } \
} while (0)

/** Assert two C strings are equal. */
#define TEST_ASSERT_STREQ(a, b) do { \
    if (strcmp((a), (b)) == 0) { \
        _pass_count++; \
    } else { \
        fprintf(stderr, "  FAIL  %s:%d  strcmp(" #a ", " #b ")\n" \
                "        got      \"%s\"\n" \
                "        expected \"%s\"\n", \
                __FILE__, __LINE__, (a), (b)); \
        _fail_count++; \
    } \
} while (0)

#define TEST_ASSERT_NULL(p)     TEST_ASSERT((p) == NULL)
#define TEST_ASSERT_NOT_NULL(p) TEST_ASSERT((p) != NULL)

/** Assert and abort on failure (for preconditions inside test helpers). */
#define TEST_ASSERT_FATAL(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FATAL %s:%d  " #cond "\n", __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* ------------------------------------------------------------------ */
/* Test runner                                                         */
/* ------------------------------------------------------------------ */

#define TEST_RUN(func) do { \
    int _before = _fail_count; \
    printf("[RUN ] " #func "\n"); \
    func(); \
    printf("[%s] " #func "\n", \
           (_fail_count == _before) ? "PASS" : "FAIL"); \
} while (0)

/** Print summary and return an exit code suitable for main(). */
#define TEST_SUMMARY() do { \
    printf("\n%d passed, %d failed\n", _pass_count, _fail_count); \
    return (_fail_count > 0) ? 1 : 0; \
} while (0)

/* ------------------------------------------------------------------ */
/* File helpers                                                        */
/* ------------------------------------------------------------------ */

/** Remove a file, ignoring ENOENT. */
static inline void remove_file(const char *path)
{
    if (unlink(path) != 0 && errno != ENOENT)
        perror("unlink");
}

#endif /* TEST_HELPERS_H */
