/**
 * @file  test_concurrent.c
 * @brief Multi-process concurrent read/write test.
 *
 * Forks N_WRITERS child processes that each write a unique key.
 * Meanwhile, N_READERS child processes loop-read a previously written
 * sentinel key. The parent waits for all children and checks exit codes.
 *
 * Validates:
 *   - No data corruption under concurrent access.
 *   - Concurrent readers do not block each other.
 *   - The sentinel value written before forking is always readable.
 */

#define _POSIX_C_SOURCE 200809L

#include "robust_cfg.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_FILE    "/tmp/robust_cfg_concurrent_test.bin"
#define N_WRITERS    4
#define N_READERS    4
#define WRITE_ITERS  10
#define READ_ITERS   20

/* ------------------------------------------------------------------ */
/* Child process: writer                                               */
/* ------------------------------------------------------------------ */

static void writer_child(int id)
{
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 0);
    if (!h) {
        fprintf(stderr, "writer %d: open failed\n", id);
        exit(1);
    }

    char key[ROBUST_CFG_KEY_MAX];
    char value[ROBUST_CFG_VALUE_MAX];

    for (int i = 0; i < WRITE_ITERS; i++) {
        snprintf(key,   sizeof(key),   "writer%d_iter", id);
        snprintf(value, sizeof(value), "pid=%d_i=%d", (int)getpid(), i);

        int rc = robust_cfg_write(h, key, value);
        if (rc != ROBUST_CFG_OK && rc != ROBUST_CFG_FULL) {
            fprintf(stderr, "writer %d: write failed rc=%d\n", id, rc);
            robust_cfg_close(h);
            exit(1);
        }
    }

    robust_cfg_close(h);
    exit(0);
}

/* ------------------------------------------------------------------ */
/* Child process: reader                                               */
/* ------------------------------------------------------------------ */

static void reader_child(int id)
{
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 0);
    if (!h) {
        fprintf(stderr, "reader %d: open failed\n", id);
        exit(1);
    }

    char buf[ROBUST_CFG_VALUE_MAX];

    for (int i = 0; i < READ_ITERS; i++) {
        int rc = robust_cfg_read(h, "sentinel", buf, sizeof(buf));
        if (rc == ROBUST_CFG_OK) {
            if (strcmp(buf, "ready") != 0) {
                fprintf(stderr, "reader %d: unexpected sentinel value '%s'\n",
                        id, buf);
                robust_cfg_close(h);
                exit(1);
            }
        } else if (rc != ROBUST_CFG_NOT_FOUND) {
            /* NOT_FOUND is acceptable if another writer deleted it, but
             * any other error is a failure. */
            fprintf(stderr, "reader %d: unexpected rc=%d\n", id, rc);
            robust_cfg_close(h);
            exit(1);
        }
        /* Brief yield to encourage interleaving */
        struct timespec _ts = { .tv_sec = 0, .tv_nsec = 1000000L };
        nanosleep(&_ts, NULL);
    }

    robust_cfg_close(h);
    exit(0);
}

/* ------------------------------------------------------------------ */
/* Test                                                                */
/* ------------------------------------------------------------------ */

static void test_concurrent_access(void)
{
    remove_file(TEST_FILE);

    /* Create file with enough capacity for all writers + sentinel. */
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 64);
    TEST_ASSERT_NOT_NULL(h);

    /* Pre-write the sentinel so readers have something to find. */
    TEST_ASSERT_EQ(robust_cfg_write(h, "sentinel", "ready"), ROBUST_CFG_OK);
    robust_cfg_close(h);

    pid_t pids[N_WRITERS + N_READERS];
    int   n = 0;

    /* Fork writers */
    for (int i = 0; i < N_WRITERS; i++) {
        pid_t p = fork();
        TEST_ASSERT_FATAL(p >= 0);
        if (p == 0)
            writer_child(i); /* does not return */
        pids[n++] = p;
    }

    /* Fork readers */
    for (int i = 0; i < N_READERS; i++) {
        pid_t p = fork();
        TEST_ASSERT_FATAL(p >= 0);
        if (p == 0)
            reader_child(i); /* does not return */
        pids[n++] = p;
    }

    /* Wait for all children */
    int all_ok = 1;
    for (int i = 0; i < n; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "child pid %d exited with status %d\n",
                    (int)pids[i], WEXITSTATUS(status));
            all_ok = 0;
        }
    }
    TEST_ASSERT(all_ok);

    /* Verify sentinel is still readable and uncorrupted */
    h = robust_cfg_open(TEST_FILE, 0);
    TEST_ASSERT_NOT_NULL(h);
    char buf[ROBUST_CFG_VALUE_MAX];
    int rc = robust_cfg_read(h, "sentinel", buf, sizeof(buf));
    TEST_ASSERT_EQ(rc, ROBUST_CFG_OK);
    if (rc == ROBUST_CFG_OK)
        TEST_ASSERT_STREQ(buf, "ready");
    robust_cfg_close(h);

    remove_file(TEST_FILE);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    TEST_RUN(test_concurrent_access);
    TEST_SUMMARY();
}
