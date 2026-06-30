/**
 * @file   robust_cfg.h
 * @brief  Public API for robust-binary-config library
 *
 * Thread-safe, process-safe Key-Value configuration store for Linux edge
 * devices. Uses CRC32 for data integrity, fcntl byte-range locking for
 * concurrent multi-process access, and log-structured writes with fdatasync
 * for power-loss durability.
 *
 * Typical usage:
 * @code
 *   robust_cfg_handle_t *h = robust_cfg_open("/etc/myapp/config.bin", 0);
 *   if (!h) { perror("open"); exit(1); }
 *
 *   robust_cfg_write(h, "server_url", "https://example.com");
 *
 *   char url[ROBUST_CFG_VALUE_MAX];
 *   if (robust_cfg_read(h, "server_url", url, sizeof(url)) == ROBUST_CFG_OK)
 *       printf("url = %s\n", url);
 *
 *   robust_cfg_close(h);
 * @endcode
 *
 * @version 1.0.0
 */
#ifndef ROBUST_CFG_H
#define ROBUST_CFG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Version                                                             */
/* ------------------------------------------------------------------ */

#define ROBUST_CFG_VERSION_MAJOR 1
#define ROBUST_CFG_VERSION_MINOR 0
#define ROBUST_CFG_VERSION_PATCH 0

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

/** Default number of record slots when creating a new file. */
#define ROBUST_CFG_DEFAULT_CAPACITY  64U

/**
 * Maximum key length including the null terminator.
 * Keys longer than ROBUST_CFG_KEY_MAX-1 characters are rejected with
 * ROBUST_CFG_BAD_ARG.
 */
#define ROBUST_CFG_KEY_MAX    32

/**
 * Maximum value length including the null terminator.
 * Values longer than ROBUST_CFG_VALUE_MAX-1 characters are rejected with
 * ROBUST_CFG_BAD_ARG.
 */
#define ROBUST_CFG_VALUE_MAX  256

/* ------------------------------------------------------------------ */
/* Return codes                                                        */
/* ------------------------------------------------------------------ */

#define ROBUST_CFG_OK          0  /**< Operation succeeded.                  */
#define ROBUST_CFG_ERR        -1  /**< Generic I/O or system error.          */
#define ROBUST_CFG_NOT_FOUND  -2  /**< The requested key does not exist.     */
#define ROBUST_CFG_CORRUPT    -3  /**< CRC mismatch — data corruption.       */
#define ROBUST_CFG_FULL       -4  /**< No empty slots available for writing. */
#define ROBUST_CFG_TIMEOUT    -5  /**< Could not acquire lock within timeout.*/
#define ROBUST_CFG_BAD_ARG    -6  /**< NULL pointer or out-of-range argument.*/

/* ------------------------------------------------------------------ */
/* Opaque handle                                                       */
/* ------------------------------------------------------------------ */

/** Opaque handle representing an open configuration file. */
typedef struct robust_cfg_handle robust_cfg_handle_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Open (or create) a binary configuration file.
 *
 * If the file does not exist it is created and initialised with
 * @p capacity empty slots. If it already exists its header magic,
 * version, and CRC are validated; all slots are scanned and any slot
 * whose CRC does not match is permanently marked CORRUPT.
 *
 * @param path      Path to the binary config file (created if absent).
 * @param capacity  Number of record slots for a newly created file.
 *                  Pass 0 to use ROBUST_CFG_DEFAULT_CAPACITY.
 *                  Ignored when opening an existing file.
 * @return          Non-NULL handle on success; NULL on failure (errno set).
 */
robust_cfg_handle_t *robust_cfg_open(const char *path, uint32_t capacity);

/**
 * @brief Close a configuration handle and release all resources.
 *
 * The underlying file descriptor is closed. Safe to call with NULL.
 *
 * @param h  Handle previously returned by robust_cfg_open().
 */
void robust_cfg_close(robust_cfg_handle_t *h);

/* ------------------------------------------------------------------ */
/* Core KV operations                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Write (or update) a key-value record.
 *
 * A new slot is written with CRC, fsynced, and verified before the old
 * slot (if any) is marked DELETED. On power loss between steps the old
 * value remains readable — at most one version of data can be lost.
 *
 * @param h      Open handle.
 * @param key    Null-terminated key (at most ROBUST_CFG_KEY_MAX-1 chars).
 * @param value  Null-terminated value (at most ROBUST_CFG_VALUE_MAX-1 chars).
 * @return       ROBUST_CFG_OK on success; ROBUST_CFG_FULL if no empty slot
 *               is available; other negative code on failure.
 */
int robust_cfg_write(robust_cfg_handle_t *h,
                     const char *key, const char *value);

/**
 * @brief Read the current value for a key.
 *
 * Multiple processes may call robust_cfg_read() concurrently without
 * blocking each other.
 *
 * @param h       Open handle.
 * @param key     Null-terminated key to look up.
 * @param value   Caller-supplied output buffer.
 * @param buflen  Size of @p value buffer in bytes (>= ROBUST_CFG_VALUE_MAX
 *                recommended to avoid truncation).
 * @return        ROBUST_CFG_OK on success; ROBUST_CFG_NOT_FOUND if the key
 *                does not exist; ROBUST_CFG_CORRUPT if the stored CRC does
 *                not match; other negative code on failure.
 */
int robust_cfg_read(robust_cfg_handle_t *h, const char *key,
                    char *value, size_t buflen);

/**
 * @brief Mark a key as deleted.
 *
 * The slot is transitioned to DELETED state rather than immediately
 * reclaimed. Use robust_cfg_compact() to reclaim space.
 *
 * @return ROBUST_CFG_OK on success; ROBUST_CFG_NOT_FOUND if absent.
 */
int robust_cfg_delete(robust_cfg_handle_t *h, const char *key);

/* ------------------------------------------------------------------ */
/* Maintenance                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Repair the configuration file after detected corruption.
 *
 * Rescans every slot, resets CORRUPT slots back to EMPTY so their space
 * may be reused, and updates the header record count. Safe to call at
 * startup after an unclean shutdown.
 *
 * @return ROBUST_CFG_OK on success; negative error code on failure.
 */
int robust_cfg_repair(robust_cfg_handle_t *h);

/**
 * @brief Compact the configuration file.
 *
 * Removes all DELETED and CORRUPT slots by rewriting valid records
 * compactly from slot 0 and zeroing the remaining slots. Reclaims space
 * for future writes. This operation holds an exclusive write lock on the
 * entire file for its duration.
 *
 * @return ROBUST_CFG_OK on success; negative error code on failure.
 */
int robust_cfg_compact(robust_cfg_handle_t *h);

#ifdef __cplusplus
}
#endif

#endif /* ROBUST_CFG_H */
