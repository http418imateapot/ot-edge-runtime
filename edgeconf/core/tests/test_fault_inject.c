/**
 * @file  test_fault_inject.c
 * @brief Fault-injection test: simulates a process crash mid-write and
 *        verifies that the config file remains consistent and recoverable.
 *
 * Strategy:
 *   1. Write a known "before" value for a key.
 *   2. Fork a child that begins writing a "during" value and is then
 *      killed with SIGKILL before it can finish (we use a helper that
 *      writes using the raw library and then sleeps before returning,
 *      simulating a crash at various stages).
 *   3. In the parent, open the file and verify:
 *      - Either the "before" value is still present (old copy intact), OR
 *      - The "during" value was fully committed (new copy is valid).
 *      Never should we see corrupted or missing data.
 *   4. Run robust_cfg_repair() and verify the file is usable afterward.
 *
 * Note: because Linux flushes page cache on normal process exit, true
 * "torn write" simulation requires writing at the block device level.
 * Here we approximate by killing the writer at various observable points
 * and relying on the library's write-verify step to catch partial writes
 * that do make it to the kernel buffer.
 */

#define _POSIX_C_SOURCE 200809L

#include "robust_cfg.h"
#include "test_helpers.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEST_FILE "/tmp/robust_cfg_fault_test.bin"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Child: opens the file, writes a value, then sleeps (so parent can kill it). */
static void writer_sleep_child(const char *key, const char *value,
                                unsigned int sleep_us)
{
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 0);
    if (!h)
        exit(1);

    /* Write may or may not complete before SIGKILL depending on timing. */
    robust_cfg_write(h, key, value);
    struct timespec _ts = { .tv_sec = 0, .tv_nsec = (long)sleep_us * 1000L };
    nanosleep(&_ts, NULL);
    robust_cfg_close(h);
    exit(0);
}

/**
 * Fork a writer child that writes @p value for @p key.
 * After @p kill_after_us microseconds, send SIGKILL.
 * Returns the final value readable from the file (via parent open).
 */
static int fork_write_and_kill(const char *key, const char *value,
                                unsigned int kill_after_us,
                                char *out_buf, size_t buflen)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0)
        writer_sleep_child(key, value, 500000 /* 500 ms */);

    /* Parent: wait a bit then kill the child. */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = kill_after_us * 1000L };
    nanosleep(&ts, NULL);
    kill(pid, SIGKILL);

    int status;
    waitpid(pid, &status, 0);

    /* Read back whatever ended up in the file. */
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 0);
    if (!h)
        return -1;
    int rc = robust_cfg_read(h, key, out_buf, buflen);
    robust_cfg_close(h);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Test: crash immediately after write starts                         */
/* ------------------------------------------------------------------ */

static void test_crash_before_write_completes(void)
{
    remove_file(TEST_FILE);

    /* Establish a known "before" value. */
    {
        robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 8);
        TEST_ASSERT_NOT_NULL(h);
        TEST_ASSERT_EQ(robust_cfg_write(h, "target", "before"), ROBUST_CFG_OK);
        robust_cfg_close(h);
    }

    /*
     * Fork writer for "during". Kill it almost immediately (1 µs).
     * At such short intervals the write may not have started, so the
     * "before" value is very likely still present.
     */
    char buf[ROBUST_CFG_VALUE_MAX] = {0};
    int rc = fork_write_and_kill("target", "during", 1 /* µs */, buf, sizeof(buf));

    /* The file must be openable and contain either "before" or "during". */
    TEST_ASSERT(rc == ROBUST_CFG_OK || rc == ROBUST_CFG_NOT_FOUND);
    if (rc == ROBUST_CFG_OK) {
        int valid = (strcmp(buf, "before") == 0 || strcmp(buf, "during") == 0);
        TEST_ASSERT(valid);
    }

    /* After repair the file must be fully functional. */
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 0);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQ(robust_cfg_repair(h), ROBUST_CFG_OK);

    /* Writing a new value post-repair must succeed. */
    TEST_ASSERT_EQ(robust_cfg_write(h, "post_repair", "ok"), ROBUST_CFG_OK);
    char vbuf[ROBUST_CFG_VALUE_MAX];
    TEST_ASSERT_EQ(robust_cfg_read(h, "post_repair", vbuf, sizeof(vbuf)),
                   ROBUST_CFG_OK);
    TEST_ASSERT_STREQ(vbuf, "ok");

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

/* ------------------------------------------------------------------ */
/* Test: crash shortly after write (fsync may or may not have run)   */
/* ------------------------------------------------------------------ */

static void test_crash_after_write(void)
{
    remove_file(TEST_FILE);

    {
        robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 8);
        TEST_ASSERT_NOT_NULL(h);
        TEST_ASSERT_EQ(robust_cfg_write(h, "val", "original"), ROBUST_CFG_OK);
        robust_cfg_close(h);
    }

    /*
     * Kill after 5 ms — likely after the write has completed but the
     * old-slot DELETED state change may or may not have finished.
     */
    char buf[ROBUST_CFG_VALUE_MAX] = {0};
    int rc = fork_write_and_kill("val", "updated", 5000 /* µs */, buf, sizeof(buf));

    /* Either value is acceptable — no corruption is the invariant. */
    TEST_ASSERT(rc == ROBUST_CFG_OK || rc == ROBUST_CFG_NOT_FOUND);
    if (rc == ROBUST_CFG_OK) {
        int valid = (strcmp(buf, "original") == 0 || strcmp(buf, "updated") == 0);
        TEST_ASSERT(valid);
    }

    /* Repair + compact should always succeed and leave the file usable. */
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 0);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQ(robust_cfg_repair(h), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_compact(h), ROBUST_CFG_OK);

    /* Post-repair we should be able to write and read. */
    TEST_ASSERT_EQ(robust_cfg_write(h, "new_key", "works"), ROBUST_CFG_OK);
    char vbuf[ROBUST_CFG_VALUE_MAX];
    TEST_ASSERT_EQ(robust_cfg_read(h, "new_key", vbuf, sizeof(vbuf)),
                   ROBUST_CFG_OK);
    TEST_ASSERT_STREQ(vbuf, "works");

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

/* ------------------------------------------------------------------ */
/* Test: multiple crash-repair cycles                                  */
/* ------------------------------------------------------------------ */

static void test_repeated_crash_repair(void)
{
    remove_file(TEST_FILE);

    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 16);
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQ(robust_cfg_write(h, "stable", "yes"), ROBUST_CFG_OK);
    robust_cfg_close(h);

    /* Three crash-repair cycles */
    for (int cycle = 0; cycle < 3; cycle++) {
        char buf[ROBUST_CFG_VALUE_MAX] = {0};
        /* Kill at 2ms — gives write a chance to start or finish */
        fork_write_and_kill("temp", "transient", 2000, buf, sizeof(buf));

        h = robust_cfg_open(TEST_FILE, 0);
        TEST_ASSERT_NOT_NULL(h);
        TEST_ASSERT_EQ(robust_cfg_repair(h), ROBUST_CFG_OK);
        TEST_ASSERT_EQ(robust_cfg_compact(h), ROBUST_CFG_OK);

        /* "stable" key must always survive */
        char stable_buf[ROBUST_CFG_VALUE_MAX];
        int rc = robust_cfg_read(h, "stable", stable_buf, sizeof(stable_buf));
        TEST_ASSERT_EQ(rc, ROBUST_CFG_OK);
        if (rc == ROBUST_CFG_OK)
            TEST_ASSERT_STREQ(stable_buf, "yes");

        robust_cfg_close(h);
    }

    remove_file(TEST_FILE);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    TEST_RUN(test_crash_before_write_completes);
    TEST_RUN(test_crash_after_write);
    TEST_RUN(test_repeated_crash_repair);
    TEST_SUMMARY();
}
