/* src/config_io.h
 *
 * Config file I/O with industrial-grade safety guarantees:
 *
 *   - config_write_atomic()  writes to a .tmp file under LOCK_EX then renames
 *     into place, so readers always see a complete, consistent file.
 *   - config_read()          acquires a shared lock so it cannot interleave
 *     with an in-progress atomic write.
 *   - config_diff()          computes which keys changed between two snapshots,
 *     letting the watch daemon broadcast only delta signals.
 *
 * Config file format (key=value, one entry per line, # comments):
 *
 *   # robust-config
 *   sample_rate=100
 *   threshold=0.85
 *   upload_url=https://example.com/upload
 *   model_path=/opt/models/v2.bin
 */
#ifndef CONFIG_IO_H
#define CONFIG_IO_H

#include <sys/types.h>

#define CONFIG_MAX_KEY    128
#define CONFIG_MAX_VALUE  256
#define CONFIG_MAX_PAIRS   64

typedef struct {
    char key[CONFIG_MAX_KEY];
    char value[CONFIG_MAX_VALUE];
} config_pair_t;

typedef struct {
    config_pair_t pairs[CONFIG_MAX_PAIRS];
    int           count;
} config_t;

/* ---------------------------------------------------------------------------
 * Core I/O
 * -------------------------------------------------------------------------*/

/* Atomically read the config at path, update key=value, and write it back.
 * Serialises concurrent writers using a per-file advisory lock (.lock file).
 * Returns 0 on success (including no-op if value is unchanged), -1 on error. */
int config_update_key(const char *path, const char *key, const char *value);

/* Atomically write cfg to path (LOCK_EX on .tmp → rename → unlock).
 * Creates parent directory components as needed.
 * Returns 0 on success, -1 on error (error logged internally). */
int config_write_atomic(const char *path, const config_t *cfg);

/* Read config file under a shared lock.  On success returns 0 and fills cfg.
 * Returns -1 if the file cannot be opened or locked. */
int config_read(const char *path, config_t *cfg);

/* ---------------------------------------------------------------------------
 * Key/value helpers
 * -------------------------------------------------------------------------*/

/* Return the value string for key, or NULL if not present. */
const char *config_get(const config_t *cfg, const char *key);

/* Insert or update key=value in cfg.
 * Returns 0 on success, -1 if cfg is full (CONFIG_MAX_PAIRS reached). */
int config_set(config_t *cfg, const char *key, const char *value);

/* Parse a "key=value" (or "key = value") line into key and value buffers.
 * Trims leading/trailing whitespace from both sides of '='.
 * Lines starting with '#' or containing no '=' return -1 (skip silently). */
int config_parse_line(const char *line,
                      char *key,   size_t klen,
                      char *value, size_t vlen);

/* ---------------------------------------------------------------------------
 * Diff helper used by watch mode
 * -------------------------------------------------------------------------*/

/* Compare old_cfg with new_cfg.  For every key whose value changed (or that
 * is new in new_cfg), append a config_pair_t to the changed[] array.
 * Returns the number of changed pairs (≤ max_changed). */
int config_diff(const config_t *old_cfg, const config_t *new_cfg,
                config_pair_t *changed, int max_changed);

#endif /* CONFIG_IO_H */
