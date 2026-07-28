/**
 * @file  test_read_write.c
 * @brief Unit tests for basic read, write, delete, repair, compact, and
 *        edge-case handling in the robust-binary-config library.
 */

#define _POSIX_C_SOURCE 200809L

#include "robust_cfg.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

#define TEST_FILE "/tmp/robust_cfg_rw_test.bin"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static robust_cfg_handle_t *open_fresh(void)
{
    remove_file(TEST_FILE);
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 16);
    TEST_ASSERT_FATAL(h != NULL);
    return h;
}

/* ------------------------------------------------------------------ */
/* Test cases                                                          */
/* ------------------------------------------------------------------ */

static void test_open_create(void)
{
    remove_file(TEST_FILE);
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 8);
    TEST_ASSERT_NOT_NULL(h);
    robust_cfg_close(h);

    /* Re-open existing file — should succeed. */
    h = robust_cfg_open(TEST_FILE, 0);
    TEST_ASSERT_NOT_NULL(h);
    robust_cfg_close(h);

    remove_file(TEST_FILE);
}

static void test_write_and_read(void)
{
    robust_cfg_handle_t *h = open_fresh();

    TEST_ASSERT_EQ(robust_cfg_write(h, "key1", "value1"), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_write(h, "key2", "value2"), ROBUST_CFG_OK);

    char buf[ROBUST_CFG_VALUE_MAX];
    TEST_ASSERT_EQ(robust_cfg_read(h, "key1", buf, sizeof(buf)), ROBUST_CFG_OK);
    TEST_ASSERT_STREQ(buf, "value1");

    TEST_ASSERT_EQ(robust_cfg_read(h, "key2", buf, sizeof(buf)), ROBUST_CFG_OK);
    TEST_ASSERT_STREQ(buf, "value2");

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

static void test_update_key(void)
{
    robust_cfg_handle_t *h = open_fresh();

    TEST_ASSERT_EQ(robust_cfg_write(h, "sensor", "22.5"), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_write(h, "sensor", "25.0"), ROBUST_CFG_OK);

    char buf[ROBUST_CFG_VALUE_MAX];
    TEST_ASSERT_EQ(robust_cfg_read(h, "sensor", buf, sizeof(buf)), ROBUST_CFG_OK);
    TEST_ASSERT_STREQ(buf, "25.0");

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

static void test_delete_key(void)
{
    robust_cfg_handle_t *h = open_fresh();

    TEST_ASSERT_EQ(robust_cfg_write(h, "tmp_key", "temporary"), ROBUST_CFG_OK);

    char buf[ROBUST_CFG_VALUE_MAX];
    TEST_ASSERT_EQ(robust_cfg_read(h, "tmp_key", buf, sizeof(buf)), ROBUST_CFG_OK);

    TEST_ASSERT_EQ(robust_cfg_delete(h, "tmp_key"), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_read(h, "tmp_key", buf, sizeof(buf)), ROBUST_CFG_NOT_FOUND);

    /* Deleting non-existent key */
    TEST_ASSERT_EQ(robust_cfg_delete(h, "nonexistent"), ROBUST_CFG_NOT_FOUND);

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

static void test_read_not_found(void)
{
    robust_cfg_handle_t *h = open_fresh();

    char buf[ROBUST_CFG_VALUE_MAX];
    TEST_ASSERT_EQ(robust_cfg_read(h, "ghost", buf, sizeof(buf)),
                   ROBUST_CFG_NOT_FOUND);

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

static void test_persist_across_reopen(void)
{
    remove_file(TEST_FILE);

    {
        robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 8);
        TEST_ASSERT_NOT_NULL(h);
        TEST_ASSERT_EQ(robust_cfg_write(h, "persist", "yes"), ROBUST_CFG_OK);
        robust_cfg_close(h);
    }

    {
        robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 0);
        TEST_ASSERT_NOT_NULL(h);
        char buf[ROBUST_CFG_VALUE_MAX];
        TEST_ASSERT_EQ(robust_cfg_read(h, "persist", buf, sizeof(buf)),
                       ROBUST_CFG_OK);
        TEST_ASSERT_STREQ(buf, "yes");
        robust_cfg_close(h);
    }

    remove_file(TEST_FILE);
}

static void test_full_capacity(void)
{
    remove_file(TEST_FILE);
    /* Create file with capacity 4 */
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 4);
    TEST_ASSERT_NOT_NULL(h);

    char key[ROBUST_CFG_KEY_MAX];
    for (int i = 0; i < 4; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        TEST_ASSERT_EQ(robust_cfg_write(h, key, "v"), ROBUST_CFG_OK);
    }
    /* 5th unique key should fail with FULL */
    TEST_ASSERT_EQ(robust_cfg_write(h, "k_extra", "v"), ROBUST_CFG_FULL);

    /* Updating an existing key must still succeed (reuses a slot). */
    TEST_ASSERT_EQ(robust_cfg_write(h, "k0", "updated"), ROBUST_CFG_OK);

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

static void test_compact_reclaims_space(void)
{
    remove_file(TEST_FILE);
    robust_cfg_handle_t *h = robust_cfg_open(TEST_FILE, 4);
    TEST_ASSERT_NOT_NULL(h);

    TEST_ASSERT_EQ(robust_cfg_write(h, "a", "1"), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_write(h, "b", "2"), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_write(h, "c", "3"), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_write(h, "d", "4"), ROBUST_CFG_OK);
    /* Full */
    TEST_ASSERT_EQ(robust_cfg_write(h, "e", "5"), ROBUST_CFG_FULL);

    TEST_ASSERT_EQ(robust_cfg_delete(h, "b"), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_delete(h, "d"), ROBUST_CFG_OK);

    TEST_ASSERT_EQ(robust_cfg_compact(h), ROBUST_CFG_OK);

    /* After compact, 'a' and 'c' still readable */
    char buf[ROBUST_CFG_VALUE_MAX];
    TEST_ASSERT_EQ(robust_cfg_read(h, "a", buf, sizeof(buf)), ROBUST_CFG_OK);
    TEST_ASSERT_STREQ(buf, "1");
    TEST_ASSERT_EQ(robust_cfg_read(h, "c", buf, sizeof(buf)), ROBUST_CFG_OK);
    TEST_ASSERT_STREQ(buf, "3");

    /* Now there is space for new keys again */
    TEST_ASSERT_EQ(robust_cfg_write(h, "e", "5"), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_write(h, "f", "6"), ROBUST_CFG_OK);

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

static void test_repair(void)
{
    robust_cfg_handle_t *h = open_fresh();

    TEST_ASSERT_EQ(robust_cfg_write(h, "keep", "alive"), ROBUST_CFG_OK);
    TEST_ASSERT_EQ(robust_cfg_repair(h), ROBUST_CFG_OK);

    char buf[ROBUST_CFG_VALUE_MAX];
    TEST_ASSERT_EQ(robust_cfg_read(h, "keep", buf, sizeof(buf)), ROBUST_CFG_OK);
    TEST_ASSERT_STREQ(buf, "alive");

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

static void test_bad_arg_handling(void)
{
    robust_cfg_handle_t *h = open_fresh();
    char buf[ROBUST_CFG_VALUE_MAX];

    /* NULL handle */
    TEST_ASSERT_EQ(robust_cfg_write(NULL, "k", "v"), ROBUST_CFG_BAD_ARG);
    TEST_ASSERT_EQ(robust_cfg_read(NULL, "k", buf, sizeof(buf)), ROBUST_CFG_BAD_ARG);
    TEST_ASSERT_EQ(robust_cfg_delete(NULL, "k"), ROBUST_CFG_BAD_ARG);
    TEST_ASSERT_EQ(robust_cfg_repair(NULL), ROBUST_CFG_BAD_ARG);
    TEST_ASSERT_EQ(robust_cfg_compact(NULL), ROBUST_CFG_BAD_ARG);

    /* NULL key / value */
    TEST_ASSERT_EQ(robust_cfg_write(h, NULL, "v"), ROBUST_CFG_BAD_ARG);
    TEST_ASSERT_EQ(robust_cfg_write(h, "k", NULL), ROBUST_CFG_BAD_ARG);
    TEST_ASSERT_EQ(robust_cfg_read(h, NULL, buf, sizeof(buf)), ROBUST_CFG_BAD_ARG);

    /* Zero-length output buffer */
    TEST_ASSERT_EQ(robust_cfg_read(h, "k", buf, 0), ROBUST_CFG_BAD_ARG);

    /* Key too long */
    char long_key[ROBUST_CFG_KEY_MAX + 5];
    memset(long_key, 'x', sizeof(long_key) - 1);
    long_key[sizeof(long_key) - 1] = '\0';
    TEST_ASSERT_EQ(robust_cfg_write(h, long_key, "v"), ROBUST_CFG_BAD_ARG);

    /* Value too long */
    char long_val[ROBUST_CFG_VALUE_MAX + 5];
    memset(long_val, 'y', sizeof(long_val) - 1);
    long_val[sizeof(long_val) - 1] = '\0';
    TEST_ASSERT_EQ(robust_cfg_write(h, "k", long_val), ROBUST_CFG_BAD_ARG);

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

static void test_max_length_key_value(void)
{
    robust_cfg_handle_t *h = open_fresh();

    /* Max allowed key: ROBUST_CFG_KEY_MAX-1 chars */
    char max_key[ROBUST_CFG_KEY_MAX];
    memset(max_key, 'k', ROBUST_CFG_KEY_MAX - 1);
    max_key[ROBUST_CFG_KEY_MAX - 1] = '\0';

    /* Max allowed value: ROBUST_CFG_VALUE_MAX-1 chars */
    char max_val[ROBUST_CFG_VALUE_MAX];
    memset(max_val, 'v', ROBUST_CFG_VALUE_MAX - 1);
    max_val[ROBUST_CFG_VALUE_MAX - 1] = '\0';

    TEST_ASSERT_EQ(robust_cfg_write(h, max_key, max_val), ROBUST_CFG_OK);

    char buf[ROBUST_CFG_VALUE_MAX];
    TEST_ASSERT_EQ(robust_cfg_read(h, max_key, buf, sizeof(buf)), ROBUST_CFG_OK);
    TEST_ASSERT_STREQ(buf, max_val);

    robust_cfg_close(h);
    remove_file(TEST_FILE);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    TEST_RUN(test_open_create);
    TEST_RUN(test_write_and_read);
    TEST_RUN(test_update_key);
    TEST_RUN(test_delete_key);
    TEST_RUN(test_read_not_found);
    TEST_RUN(test_persist_across_reopen);
    TEST_RUN(test_full_capacity);
    TEST_RUN(test_compact_reclaims_space);
    TEST_RUN(test_repair);
    TEST_RUN(test_bad_arg_handling);
    TEST_RUN(test_max_length_key_value);
    TEST_SUMMARY();
}
