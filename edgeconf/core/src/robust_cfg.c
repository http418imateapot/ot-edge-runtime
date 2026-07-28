/**
 * @file   robust_cfg.c
 * @brief  Implementation of the robust-binary-config library.
 *
 * Concurrency model
 * -----------------
 * Reads  : F_RDLCK on the entire file — multiple processes may read
 *          simultaneously without blocking each other.
 * Writes : F_WRLCK on the entire file — a single writer holds an
 *          exclusive lock, blocking all other readers and writers.
 *
 * All lock acquisitions use non-blocking F_SETLK with a retry loop
 * (LOCK_RETRY_COUNT × LOCK_RETRY_SLEEP_MS) to avoid indefinite hangs
 * when a holder crashes while holding a lock.
 *
 * Durability
 * ----------
 * Every write is followed by fdatasync(). A write-verify step reads the
 * record back and compares CRCs to detect silent write failures before
 * the old copy is discarded.
 */

#define _POSIX_C_SOURCE 200809L

#include "robust_cfg.h"
#include "robust_cfg_format.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Locking parameters                                                  */
/* ------------------------------------------------------------------ */

/** Number of F_SETLK retry attempts before returning ROBUST_CFG_TIMEOUT. */
#define LOCK_RETRY_COUNT     500

/** Sleep duration between retries in milliseconds. */
#define LOCK_RETRY_SLEEP_MS  10

/* ------------------------------------------------------------------ */
/* CRC32 (standard Ethernet/ZIP polynomial 0xEDB88320)                */
/* ------------------------------------------------------------------ */

static uint32_t crc32_table[256];
static int      crc32_ready = 0;

static void crc32_init(void)
{
    const uint32_t poly = 0xEDB88320U;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1u) ? (poly ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}

static uint32_t crc32_compute(const void *buf, size_t len)
{
    if (!crc32_ready)
        crc32_init();
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

/* ------------------------------------------------------------------ */
/* Handle                                                              */
/* ------------------------------------------------------------------ */

struct robust_cfg_handle {
    int             fd;
    uint32_t        capacity;
    pthread_mutex_t mutex;
};

/* ------------------------------------------------------------------ */
/* Byte-range locking helpers                                          */
/* ------------------------------------------------------------------ */

/**
 * Acquire or release a byte-range lock on @p fd.
 *
 * @param fd      File descriptor.
 * @param type    F_RDLCK, F_WRLCK, or F_UNLCK.
 * @param offset  Start offset (SEEK_SET).
 * @param length  Byte count; 0 means "to end of file".
 * @return ROBUST_CFG_OK, ROBUST_CFG_TIMEOUT, or ROBUST_CFG_ERR.
 */
static int lock_range(int fd, short type, off_t offset, off_t length)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = type;
    fl.l_whence = SEEK_SET;
    fl.l_start  = offset;
    fl.l_len    = length;

    if (type == F_UNLCK) {
        if (fcntl(fd, F_SETLK, &fl) == -1) {
            perror("robust_cfg: F_UNLCK");
            return ROBUST_CFG_ERR;
        }
        return ROBUST_CFG_OK;
    }

    for (int i = 0; i < LOCK_RETRY_COUNT; i++) {
        if (fcntl(fd, F_SETLK, &fl) == 0)
            return ROBUST_CFG_OK;
        if (errno != EACCES && errno != EAGAIN) {
            perror("robust_cfg: lock");
            return ROBUST_CFG_ERR;
        }
        /* Brief sleep before retry */
        struct timespec ts = {
            .tv_sec  = LOCK_RETRY_SLEEP_MS / 1000,
            .tv_nsec = (LOCK_RETRY_SLEEP_MS % 1000) * 1000000L
        };
        nanosleep(&ts, NULL);
    }
    return ROBUST_CFG_TIMEOUT;
}

/** Acquire a read lock on the entire file (allows concurrent readers). */
static int lock_read(int fd)
{
    return lock_range(fd, F_RDLCK, 0, 0);
}

/** Acquire a write lock on the entire file (exclusive). */
static int lock_write(int fd)
{
    return lock_range(fd, F_WRLCK, 0, 0);
}

/** Release any lock on the entire file. */
static int unlock_file(int fd)
{
    return lock_range(fd, F_UNLCK, 0, 0);
}

/* ------------------------------------------------------------------ */
/* CRC helpers                                                         */
/* ------------------------------------------------------------------ */

static uint32_t header_crc(const robust_cfg_file_header_t *hdr)
{
    /* CRC covers every field except header_crc itself. */
    return crc32_compute(hdr, sizeof(*hdr) - sizeof(hdr->header_crc));
}

static uint32_t record_crc(const robust_cfg_record_t *rec)
{
    return crc32_compute(rec, sizeof(*rec) - sizeof(rec->record_crc));
}

/* ------------------------------------------------------------------ */
/* Low-level I/O                                                       */
/* ------------------------------------------------------------------ */

static int io_read(int fd, off_t offset, void *buf, size_t len)
{
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
        return ROBUST_CFG_ERR;
    ssize_t n = read(fd, buf, len);
    if (n < 0 || (size_t)n != len)
        return ROBUST_CFG_ERR;
    return ROBUST_CFG_OK;
}

static int io_write(int fd, off_t offset, const void *buf, size_t len)
{
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
        return ROBUST_CFG_ERR;
    ssize_t n = write(fd, buf, len);
    if (n < 0 || (size_t)n != len)
        return ROBUST_CFG_ERR;
    return ROBUST_CFG_OK;
}

/* ------------------------------------------------------------------ */
/* Header I/O                                                          */
/* ------------------------------------------------------------------ */

static int read_header(int fd, robust_cfg_file_header_t *hdr)
{
    return io_read(fd, 0, hdr, FMT_HEADER_SIZE);
}

/** Compute and write header_crc, then flush to disk. */
static int write_header(int fd, robust_cfg_file_header_t *hdr)
{
    hdr->header_crc = header_crc(hdr);
    if (io_write(fd, 0, hdr, FMT_HEADER_SIZE) != ROBUST_CFG_OK)
        return ROBUST_CFG_ERR;
    if (fdatasync(fd) != 0)
        return ROBUST_CFG_ERR;
    return ROBUST_CFG_OK;
}

/* ------------------------------------------------------------------ */
/* Record I/O                                                          */
/* ------------------------------------------------------------------ */

static int read_record(int fd, uint32_t idx, robust_cfg_record_t *rec)
{
    return io_read(fd, FMT_SLOT_OFFSET(idx), rec, FMT_RECORD_SIZE);
}

/**
 * Write a record:
 *   1. compute CRC
 *   2. write to disk
 *   3. fdatasync
 *   4. write-verify (read back and compare CRC)
 *
 * Caller must hold the file write lock.
 */
static int write_record_verified(int fd, uint32_t idx,
                                 robust_cfg_record_t *rec)
{
    rec->record_crc = record_crc(rec);

    off_t off = FMT_SLOT_OFFSET(idx);
    if (io_write(fd, off, rec, FMT_RECORD_SIZE) != ROBUST_CFG_OK)
        return ROBUST_CFG_ERR;
    if (fdatasync(fd) != 0)
        return ROBUST_CFG_ERR;

    /* Write-verify: read back and confirm CRC matches. */
    robust_cfg_record_t verify;
    if (io_read(fd, off, &verify, FMT_RECORD_SIZE) != ROBUST_CFG_OK)
        return ROBUST_CFG_ERR;
    if (verify.record_crc != rec->record_crc)
        return ROBUST_CFG_CORRUPT;

    return ROBUST_CFG_OK;
}

/**
 * Update only the state byte of a slot.
 * Recalculates the record CRC after changing state.
 * Caller must hold the file write lock.
 */
static int set_slot_state(int fd, uint32_t idx, uint8_t state)
{
    robust_cfg_record_t rec;
    if (read_record(fd, idx, &rec) != ROBUST_CFG_OK)
        return ROBUST_CFG_ERR;
    rec.state = state;
    return write_record_verified(fd, idx, &rec);
}

/* ------------------------------------------------------------------ */
/* File initialisation                                                 */
/* ------------------------------------------------------------------ */

static int init_new_file(int fd, uint32_t capacity)
{
    /* Write the file header. */
    robust_cfg_file_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, ROBUST_CFG_MAGIC_STR, 4);
    hdr.version      = ROBUST_CFG_FORMAT_VERSION;
    hdr.capacity     = capacity;
    hdr.record_count = 0;

    if (write_header(fd, &hdr) != ROBUST_CFG_OK)
        return ROBUST_CFG_ERR;

    /* Pre-populate all slots as EMPTY so the file has a defined layout. */
    robust_cfg_record_t empty_rec;
    memset(&empty_rec, 0, sizeof(empty_rec));
    empty_rec.state      = SLOT_EMPTY;
    empty_rec.record_crc = record_crc(&empty_rec);

    for (uint32_t i = 0; i < capacity; i++) {
        if (io_write(fd, FMT_SLOT_OFFSET(i), &empty_rec,
                     FMT_RECORD_SIZE) != ROBUST_CFG_OK)
            return ROBUST_CFG_ERR;
    }

    if (fdatasync(fd) != 0)
        return ROBUST_CFG_ERR;

    return ROBUST_CFG_OK;
}

/**
 * Validate the header of an existing file and scan all slots for CRC
 * errors, marking corrupt slots as SLOT_CORRUPT.
 *
 * @param fd           Open file descriptor (write-capable).
 * @param out_capacity Receives the capacity from the validated header.
 * @return ROBUST_CFG_OK, ROBUST_CFG_CORRUPT, or ROBUST_CFG_ERR.
 */
static int validate_and_scan(int fd, uint32_t *out_capacity)
{
    robust_cfg_file_header_t hdr;
    if (read_header(fd, &hdr) != ROBUST_CFG_OK)
        return ROBUST_CFG_ERR;

    /* Magic check */
    if (memcmp(hdr.magic, ROBUST_CFG_MAGIC_STR, 4) != 0) {
        fprintf(stderr, "robust_cfg: bad magic — not a robust-config file\n");
        return ROBUST_CFG_CORRUPT;
    }

    /* Header CRC */
    if (hdr.header_crc != header_crc(&hdr)) {
        fprintf(stderr, "robust_cfg: header CRC mismatch\n");
        return ROBUST_CFG_CORRUPT;
    }

    /* Format version */
    if (hdr.version != ROBUST_CFG_FORMAT_VERSION) {
        fprintf(stderr, "robust_cfg: unsupported format version %u "
                "(expected %u)\n", hdr.version, ROBUST_CFG_FORMAT_VERSION);
        return ROBUST_CFG_ERR;
    }

    *out_capacity = hdr.capacity;

    /* Scan slots; mark CRC-invalid slots as CORRUPT. */
    uint32_t valid_count = 0;
    for (uint32_t i = 0; i < hdr.capacity; i++) {
        robust_cfg_record_t rec;
        if (read_record(fd, i, &rec) != ROBUST_CFG_OK)
            continue;

        if (rec.state == SLOT_VALID || rec.state == SLOT_DELETED) {
            if (rec.record_crc != record_crc(&rec)) {
                fprintf(stderr,
                        "robust_cfg: slot %u CRC mismatch — marking CORRUPT\n",
                        i);
                /* Best-effort: if this fails we still continue. */
                (void)set_slot_state(fd, i, SLOT_CORRUPT);
                continue;
            }
        }
        if (rec.state == SLOT_VALID)
            valid_count++;
    }

    /* Resync header record_count if it drifted (e.g. after a crash). */
    if (valid_count != hdr.record_count) {
        hdr.record_count = valid_count;
        (void)write_header(fd, &hdr);
    }

    return ROBUST_CFG_OK;
}

/* ------------------------------------------------------------------ */
/* Public API — lifecycle                                              */
/* ------------------------------------------------------------------ */

robust_cfg_handle_t *robust_cfg_open(const char *path, uint32_t capacity)
{
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    if (capacity == 0)
        capacity = ROBUST_CFG_DEFAULT_CAPACITY;

    /*
     * Use O_EXCL to atomically determine whether we are creating the
     * file or opening an existing one, avoiding a TOCTOU race.
     */
    int is_new = 0;
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd >= 0) {
        is_new = 1;
    } else if (errno == EEXIST) {
        fd = open(path, O_RDWR, 0644);
        if (fd < 0) {
            perror("robust_cfg_open: open");
            return NULL;
        }
    } else {
        perror("robust_cfg_open: open");
        return NULL;
    }

    /* Acquire exclusive lock for initialisation / validation. */
    if (lock_write(fd) != ROBUST_CFG_OK) {
        close(fd);
        return NULL;
    }

    int rc;
    uint32_t actual_capacity = capacity;

    if (is_new) {
        rc = init_new_file(fd, capacity);
    } else {
        rc = validate_and_scan(fd, &actual_capacity);
    }

    unlock_file(fd);

    if (rc != ROBUST_CFG_OK) {
        close(fd);
        return NULL;
    }

    robust_cfg_handle_t *h = calloc(1, sizeof(*h));
    if (!h) {
        close(fd);
        return NULL;
    }

    h->fd       = fd;
    h->capacity = actual_capacity;
    if (pthread_mutex_init(&h->mutex, NULL) != 0) {
        close(fd);
        free(h);
        return NULL;
    }

    return h;
}

void robust_cfg_close(robust_cfg_handle_t *h)
{
    if (!h)
        return;
    close(h->fd);
    pthread_mutex_destroy(&h->mutex);
    free(h);
}

/* ------------------------------------------------------------------ */
/* Public API — KV operations                                          */
/* ------------------------------------------------------------------ */

int robust_cfg_write(robust_cfg_handle_t *h,
                     const char *key, const char *value)
{
    if (!h || !key || !value)
        return ROBUST_CFG_BAD_ARG;
    if (strlen(key) >= ROBUST_CFG_KEY_MAX)
        return ROBUST_CFG_BAD_ARG;
    if (strlen(value) >= ROBUST_CFG_VALUE_MAX)
        return ROBUST_CFG_BAD_ARG;

    pthread_mutex_lock(&h->mutex);

    int rc = lock_write(h->fd);
    if (rc != ROBUST_CFG_OK) {
        pthread_mutex_unlock(&h->mutex);
        return rc;
    }

    int      old_idx   = -1;
    int      empty_idx = -1;

    /* Single pass: find an empty slot and any existing slot for the key. */
    for (uint32_t i = 0; i < h->capacity; i++) {
        robust_cfg_record_t rec;
        if (read_record(h->fd, i, &rec) != ROBUST_CFG_OK)
            continue;

        if (rec.state == SLOT_EMPTY && empty_idx == -1) {
            empty_idx = (int)i;
        } else if (rec.state == SLOT_VALID) {
            if (rec.record_crc == record_crc(&rec) &&
                strcmp(rec.key, key) == 0) {
                old_idx = (int)i;
            }
        }
    }

    if (empty_idx == -1 && old_idx == -1) {
        unlock_file(h->fd);
        pthread_mutex_unlock(&h->mutex);
        return ROBUST_CFG_FULL;
    }

    /* Prepare the new record. */
    robust_cfg_record_t new_rec;
    memset(&new_rec, 0, sizeof(new_rec));
    new_rec.state     = SLOT_VALID;
    new_rec.timestamp = (uint32_t)time(NULL);
    strncpy(new_rec.key,   key,   FMT_KEY_MAX - 1);
    strncpy(new_rec.value, value, FMT_VALUE_MAX - 1);

    if (empty_idx != -1) {
        /*
         * Normal path: write to empty slot, then mark old as DELETED.
         * Crash-safe: old copy stays valid until new copy is verified.
         */
        int rc2 = write_record_verified(h->fd, (uint32_t)empty_idx, &new_rec);
        if (rc2 != ROBUST_CFG_OK) {
            unlock_file(h->fd);
            pthread_mutex_unlock(&h->mutex);
            return rc2;
        }

        /*
         * New record confirmed on disk. Mark old slot DELETED.
         * If interrupted here, read() picks the newest timestamp so
         * both copies are harmless.
         */
        if (old_idx >= 0)
            (void)set_slot_state(h->fd, (uint32_t)old_idx, SLOT_DELETED);
    } else {
        /*
         * In-place update path: the file is completely full but the key
         * already exists. Overwrite the existing slot directly.
         * This sacrifices the "old copy as safety net" guarantee but is
         * still fdatasync'd and write-verified.
         */
        int rc2 = write_record_verified(h->fd, (uint32_t)old_idx, &new_rec);
        if (rc2 != ROBUST_CFG_OK) {
            unlock_file(h->fd);
            pthread_mutex_unlock(&h->mutex);
            return rc2;
        }
    }

    /* Update header record_count. */
    robust_cfg_file_header_t hdr;
    if (read_header(h->fd, &hdr) == ROBUST_CFG_OK) {
        if (old_idx < 0)
            hdr.record_count++;
        (void)write_header(h->fd, &hdr);
    }

    unlock_file(h->fd);
    pthread_mutex_unlock(&h->mutex);
    return ROBUST_CFG_OK;
}

int robust_cfg_read(robust_cfg_handle_t *h, const char *key,
                    char *value, size_t buflen)
{
    if (!h || !key || !value || buflen == 0)
        return ROBUST_CFG_BAD_ARG;

    pthread_mutex_lock(&h->mutex);

    int rc = lock_read(h->fd);
    if (rc != ROBUST_CFG_OK) {
        pthread_mutex_unlock(&h->mutex);
        return rc;
    }

    /*
     * Scan all slots. If multiple VALID records exist for the same key
     * (possible after a crash mid-delete), prefer the one with the
     * greatest timestamp.
     */
    int      best_idx = -1;
    uint32_t best_ts  = 0;

    for (uint32_t i = 0; i < h->capacity; i++) {
        robust_cfg_record_t rec;
        if (read_record(h->fd, i, &rec) != ROBUST_CFG_OK)
            continue;
        if (rec.state != SLOT_VALID)
            continue;
        if (rec.record_crc != record_crc(&rec))
            continue;
        if (strcmp(rec.key, key) == 0) {
            if (best_idx == -1 || rec.timestamp >= best_ts) {
                best_idx = (int)i;
                best_ts  = rec.timestamp;
            }
        }
    }

    if (best_idx == -1) {
        unlock_file(h->fd);
        pthread_mutex_unlock(&h->mutex);
        return ROBUST_CFG_NOT_FOUND;
    }

    /* Re-read winning record under lock to guard against torn writes. */
    robust_cfg_record_t result;
    rc = read_record(h->fd, (uint32_t)best_idx, &result);

    unlock_file(h->fd);
    pthread_mutex_unlock(&h->mutex);

    if (rc != ROBUST_CFG_OK)
        return ROBUST_CFG_ERR;
    if (result.record_crc != record_crc(&result))
        return ROBUST_CFG_CORRUPT;

    strncpy(value, result.value, buflen - 1);
    value[buflen - 1] = '\0';
    return ROBUST_CFG_OK;
}

int robust_cfg_delete(robust_cfg_handle_t *h, const char *key)
{
    if (!h || !key)
        return ROBUST_CFG_BAD_ARG;

    pthread_mutex_lock(&h->mutex);

    int rc = lock_write(h->fd);
    if (rc != ROBUST_CFG_OK) {
        pthread_mutex_unlock(&h->mutex);
        return rc;
    }

    int deleted = 0;

    for (uint32_t i = 0; i < h->capacity; i++) {
        robust_cfg_record_t rec;
        if (read_record(h->fd, i, &rec) != ROBUST_CFG_OK)
            continue;
        if (rec.state != SLOT_VALID)
            continue;
        if (rec.record_crc != record_crc(&rec))
            continue;
        if (strcmp(rec.key, key) == 0) {
            if (set_slot_state(h->fd, i, SLOT_DELETED) == ROBUST_CFG_OK)
                deleted++;
        }
    }

    if (deleted > 0) {
        robust_cfg_file_header_t hdr;
        if (read_header(h->fd, &hdr) == ROBUST_CFG_OK &&
            hdr.record_count >= (uint32_t)deleted) {
            hdr.record_count -= (uint32_t)deleted;
            (void)write_header(h->fd, &hdr);
        }
    }

    unlock_file(h->fd);
    pthread_mutex_unlock(&h->mutex);
    return (deleted > 0) ? ROBUST_CFG_OK : ROBUST_CFG_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/* Public API — maintenance                                            */
/* ------------------------------------------------------------------ */

int robust_cfg_repair(robust_cfg_handle_t *h)
{
    if (!h)
        return ROBUST_CFG_BAD_ARG;

    pthread_mutex_lock(&h->mutex);

    int rc = lock_write(h->fd);
    if (rc != ROBUST_CFG_OK) {
        pthread_mutex_unlock(&h->mutex);
        return rc;
    }

    uint32_t valid_count = 0;

    for (uint32_t i = 0; i < h->capacity; i++) {
        robust_cfg_record_t rec;
        if (read_record(h->fd, i, &rec) != ROBUST_CFG_OK)
            continue;

        if (rec.state == SLOT_CORRUPT) {
            /* Reset to EMPTY so the slot can be reused. */
            memset(&rec, 0, sizeof(rec));
            rec.state      = SLOT_EMPTY;
            rec.record_crc = record_crc(&rec);
            (void)io_write(h->fd, FMT_SLOT_OFFSET(i), &rec, FMT_RECORD_SIZE);
        } else if (rec.state == SLOT_VALID) {
            if (rec.record_crc != record_crc(&rec)) {
                /* CRC mismatch found during repair — reset to EMPTY. */
                memset(&rec, 0, sizeof(rec));
                rec.state      = SLOT_EMPTY;
                rec.record_crc = record_crc(&rec);
                (void)io_write(h->fd, FMT_SLOT_OFFSET(i), &rec,
                               FMT_RECORD_SIZE);
            } else {
                valid_count++;
            }
        }
    }

    (void)fdatasync(h->fd);

    robust_cfg_file_header_t hdr;
    if (read_header(h->fd, &hdr) == ROBUST_CFG_OK) {
        hdr.record_count = valid_count;
        (void)write_header(h->fd, &hdr);
    }

    unlock_file(h->fd);
    pthread_mutex_unlock(&h->mutex);
    return ROBUST_CFG_OK;
}

int robust_cfg_compact(robust_cfg_handle_t *h)
{
    if (!h)
        return ROBUST_CFG_BAD_ARG;

    pthread_mutex_lock(&h->mutex);

    int rc = lock_write(h->fd);
    if (rc != ROBUST_CFG_OK) {
        pthread_mutex_unlock(&h->mutex);
        return rc;
    }

    /* Collect all VALID records with valid CRC. */
    robust_cfg_record_t *valid =
        calloc(h->capacity, sizeof(robust_cfg_record_t));
    if (!valid) {
        unlock_file(h->fd);
        pthread_mutex_unlock(&h->mutex);
        return ROBUST_CFG_ERR;
    }

    uint32_t valid_count = 0;
    for (uint32_t i = 0; i < h->capacity; i++) {
        robust_cfg_record_t rec;
        if (read_record(h->fd, i, &rec) != ROBUST_CFG_OK)
            continue;
        if (rec.state == SLOT_VALID && rec.record_crc == record_crc(&rec))
            valid[valid_count++] = rec;
    }

    /* Rewrite: valid records first, then EMPTY for the rest. */
    robust_cfg_record_t empty_rec;
    memset(&empty_rec, 0, sizeof(empty_rec));
    empty_rec.state      = SLOT_EMPTY;
    empty_rec.record_crc = record_crc(&empty_rec);

    for (uint32_t i = 0; i < h->capacity; i++) {
        const robust_cfg_record_t *src = (i < valid_count)
            ? &valid[i] : &empty_rec;
        (void)io_write(h->fd, FMT_SLOT_OFFSET(i), src, FMT_RECORD_SIZE);
    }
    (void)fdatasync(h->fd);

    /* Update header. */
    robust_cfg_file_header_t hdr;
    if (read_header(h->fd, &hdr) == ROBUST_CFG_OK) {
        hdr.record_count = valid_count;
        (void)write_header(h->fd, &hdr);
    }

    free(valid);
    unlock_file(h->fd);
    pthread_mutex_unlock(&h->mutex);
    return ROBUST_CFG_OK;
}
