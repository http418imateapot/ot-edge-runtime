/* src/config_io.c */
#include "config_io.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>   /* flock() */
#include <sys/stat.h>
#include <sys/types.h>

/* ---------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------*/

/* Create every component of a directory path (like mkdir -p). */
static int mkdirs(const char *path, mode_t mode) {
    char   tmp[512];
    size_t len;

    len = strlen(path);
    if (len == 0 || len >= sizeof(tmp))
        return -1;

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) == -1 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) == -1 && errno != EEXIST)
        return -1;
    return 0;
}

/* Trim trailing whitespace (spaces, \r, \n) in-place. */
static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' ||
                     s[n-1] == '\r' || s[n-1] == '\n'))
        s[--n] = '\0';
}

/* ---------------------------------------------------------------------------
 * config_parse_line
 * -------------------------------------------------------------------------*/
int config_parse_line(const char *line,
                      char *key,   size_t klen,
                      char *value, size_t vlen) {
    /* Skip blank lines and comments */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == '\0' || *line == '\r' || *line == '\n')
        return -1;

    const char *eq = strchr(line, '=');
    if (!eq)
        return -1;

    /* Copy key, trim trailing whitespace */
    size_t kl = (size_t)(eq - line);
    if (kl == 0 || kl >= klen)
        return -1;
    strncpy(key, line, kl);
    key[kl] = '\0';
    rtrim(key);
    if (key[0] == '\0')
        return -1;

    /* Copy value, skip leading whitespace, trim trailing */
    const char *vp = eq + 1;
    while (*vp == ' ' || *vp == '\t') vp++;
    size_t vl = strlen(vp);
    if (vl >= vlen)
        vl = vlen - 1;
    strncpy(value, vp, vl);
    value[vl] = '\0';
    rtrim(value);
    return 0;
}

/* ---------------------------------------------------------------------------
 * config_get / config_set
 * -------------------------------------------------------------------------*/
const char *config_get(const config_t *cfg, const char *key) {
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->pairs[i].key, key) == 0)
            return cfg->pairs[i].value;
    }
    return NULL;
}

int config_set(config_t *cfg, const char *key, const char *value) {
    /* Update existing entry */
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->pairs[i].key, key) == 0) {
            strncpy(cfg->pairs[i].value, value, CONFIG_MAX_VALUE - 1);
            cfg->pairs[i].value[CONFIG_MAX_VALUE - 1] = '\0';
            return 0;
        }
    }
    /* Append new entry */
    if (cfg->count >= CONFIG_MAX_PAIRS) {
        log_err("config_set: too many pairs (max %d)", CONFIG_MAX_PAIRS);
        return -1;
    }
    strncpy(cfg->pairs[cfg->count].key,   key,   CONFIG_MAX_KEY   - 1);
    cfg->pairs[cfg->count].key[CONFIG_MAX_KEY - 1] = '\0';
    strncpy(cfg->pairs[cfg->count].value, value, CONFIG_MAX_VALUE - 1);
    cfg->pairs[cfg->count].value[CONFIG_MAX_VALUE - 1] = '\0';
    cfg->count++;
    return 0;
}

/* ---------------------------------------------------------------------------
 * config_read
 * -------------------------------------------------------------------------*/
int config_read(const char *path, config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        if (errno != ENOENT)
            log_err("config_read: cannot open '%s': %m", path);
        return -1;
    }

    /* Shared lock: may coexist with other readers; blocks exclusive writers. */
    if (flock(fd, LOCK_SH) == -1) {
        log_err("config_read: cannot acquire shared lock on '%s': %m", path);
        close(fd);
        return -1;
    }

    FILE *fp = fdopen(fd, "r");
    if (!fp) {
        log_err("config_read: fdopen failed: %m");
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    char line[CONFIG_MAX_KEY + CONFIG_MAX_VALUE + 4];
    while (fgets(line, (int)sizeof(line), fp)) {
        char key[CONFIG_MAX_KEY], val[CONFIG_MAX_VALUE];
        if (config_parse_line(line, key, sizeof(key), val, sizeof(val)) == 0)
            config_set(cfg, key, val);   /* tolerates duplicates; last wins */
    }

    flock(fd, LOCK_UN);
    fclose(fp);   /* also closes fd */
    return 0;
}

/* ---------------------------------------------------------------------------
 * config_update_key  (atomic read-modify-write)
 * -------------------------------------------------------------------------*/
int config_update_key(const char *path, const char *key, const char *value) {
    /* Use a dedicated lock file to serialise concurrent read-modify-write cycles.
     * This prevents the TOCTOU race where two writers both read the same snapshot
     * and then each overwrites the other's changes. */
    char lock_path[512];
    if (snprintf(lock_path, sizeof(lock_path), "%s.lock", path)
            >= (int)sizeof(lock_path)) {
        log_err("config_update_key: path too long");
        return -1;
    }

    /* Ensure parent directory exists before creating the lock file */
    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        if (mkdirs(dir, 0755) == -1 && errno != EEXIST) {
            log_err("config_update_key: cannot create directory '%s': %m", dir);
            return -1;
        }
    }

    int lock_fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lock_fd == -1) {
        log_err("config_update_key: cannot open lock file '%s': %m", lock_path);
        return -1;
    }

    /* Blocking exclusive lock – serialises all concurrent writers */
    if (flock(lock_fd, LOCK_EX) == -1) {
        log_err("config_update_key: flock LOCK_EX failed: %m");
        close(lock_fd);
        return -1;
    }

    /* Read current config while holding the lock */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_read(path, &cfg);   /* OK if file does not exist yet */

    /* Check for no-op */
    const char *cur = config_get(&cfg, key);
    if (cur && strcmp(cur, value) == 0) {
        log_info("Key '%s' unchanged ('%s'); skipping write", key, value);
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return 0;
    }

    if (config_set(&cfg, key, value) != 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return -1;
    }

    /* Write atomically while still holding the exclusive lock */
    int rc = config_write_atomic(path, &cfg);

    flock(lock_fd, LOCK_UN);
    close(lock_fd);
    return rc;
}

/* ---------------------------------------------------------------------------
 * config_write_atomic
 * -------------------------------------------------------------------------*/
int config_write_atomic(const char *path, const config_t *cfg) {
    /* Build a per-process unique .tmp path to avoid concurrent-writer collisions */
    char tmp_path[512];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", path, (int)getpid())
            >= (int)sizeof(tmp_path)) {
        log_err("config_write_atomic: path too long");
        return -1;
    }

    /* Ensure parent directory exists */
    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        if (mkdirs(dir, 0755) == -1) {
            log_err("config_write_atomic: cannot create directory '%s': %m", dir);
            return -1;
        }
    }

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd == -1) {
        log_err("config_write_atomic: cannot create '%s': %m", tmp_path);
        return -1;
    }

    /* Exclusive lock on tmp file: only one writer at a time */
    if (flock(fd, LOCK_EX) == -1) {
        log_err("config_write_atomic: cannot acquire exclusive lock: %m");
        close(fd);
        unlink(tmp_path);
        return -1;
    }

    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        log_err("config_write_atomic: fdopen failed: %m");
        flock(fd, LOCK_UN);
        close(fd);
        unlink(tmp_path);
        return -1;
    }

    fprintf(fp, "# robust-config key=value store\n");
    for (int i = 0; i < cfg->count; i++)
        fprintf(fp, "%s=%s\n", cfg->pairs[i].key, cfg->pairs[i].value);

    fflush(fp);
    /* Release lock before rename so readers on the final path are not blocked */
    flock(fd, LOCK_UN);
    fclose(fp);   /* closes fd */

    /* Atomic rename: readers either see old or new, never a partial write */
    if (rename(tmp_path, path) == -1) {
        log_err("config_write_atomic: rename '%s' -> '%s' failed: %m",
                tmp_path, path);
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * config_diff
 * -------------------------------------------------------------------------*/
int config_diff(const config_t *old_cfg, const config_t *new_cfg,
                config_pair_t *changed, int max_changed) {
    int n = 0;
    for (int i = 0; i < new_cfg->count && n < max_changed; i++) {
        const char *old_val = config_get(old_cfg, new_cfg->pairs[i].key);
        if (!old_val || strcmp(old_val, new_cfg->pairs[i].value) != 0) {
            strncpy(changed[n].key,   new_cfg->pairs[i].key,
                    sizeof(changed[n].key)   - 1);
            strncpy(changed[n].value, new_cfg->pairs[i].value,
                    sizeof(changed[n].value) - 1);
            changed[n].key[sizeof(changed[n].key)     - 1] = '\0';
            changed[n].value[sizeof(changed[n].value) - 1] = '\0';
            n++;
        }
    }
    return n;
}
