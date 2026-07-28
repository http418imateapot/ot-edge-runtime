/**
 * @file   robust_cfg_format.h
 * @brief  Internal binary format definitions for robust-binary-config.
 *
 * This header is NOT part of the public API. Do not include it from
 * application code — use include/robust_cfg.h instead.
 *
 * File layout (format version 1):
 *
 *   Offset              Size    Field
 *   ──────────────────  ──────  ──────────────────────────────────────
 *   0                   64      File Header  (robust_cfg_file_header_t)
 *   64                  300     Record Slot 0 (robust_cfg_record_t)
 *   364                 300     Record Slot 1
 *   ...
 *   64 + N*300          300     Record Slot N-1   (N = capacity)
 *
 * All multi-byte integers are stored in the native byte order of the
 * writing host. Files are not expected to be shared across architectures
 * with different endianness; the magic number and version check will
 * catch accidental cross-architecture use.
 */
#ifndef ROBUST_CFG_FORMAT_H
#define ROBUST_CFG_FORMAT_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

/** Four-byte ASCII magic string at the start of every valid config file. */
#define ROBUST_CFG_MAGIC_STR   "RCFG"

/** Format version stored in the file header. Increment on breaking changes. */
#define ROBUST_CFG_FORMAT_VERSION  1U

/* ------------------------------------------------------------------ */
/* Slot state enumeration                                              */
/* ------------------------------------------------------------------ */

/**
 * State machine transitions:
 *
 *   EMPTY ──write──► VALID ──delete──► DELETED
 *     ▲                │                   │
 *     │                │ CRC mismatch      │ CRC mismatch
 *     │                ▼                   ▼
 *   repair          CORRUPT ◄──────────────┘
 *     └──────────────────┘
 */
typedef enum {
    SLOT_EMPTY   = 0x00, /**< Never written; available for allocation.     */
    SLOT_VALID   = 0x01, /**< Contains a live key-value pair.              */
    SLOT_DELETED = 0x02, /**< Logically removed; space reclaimed on compact.*/
    SLOT_CORRUPT = 0xFF  /**< CRC mismatch detected; reset by repair().    */
} slot_state_t;

/* ------------------------------------------------------------------ */
/* File header — exactly 64 bytes                                     */
/* ------------------------------------------------------------------ */

/**
 * Placed at offset 0 of the config file.
 * header_crc covers bytes 0 through 59 (all fields except header_crc itself).
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];       /**< ASCII "RCFG"                               */
    uint16_t version;        /**< Format version; must equal FORMAT_VERSION  */
    uint16_t flags;          /**< Reserved; must be 0                        */
    uint32_t capacity;       /**< Total number of record slots in the file   */
    uint32_t record_count;   /**< Cached count of VALID records (header CRC
                               *   protects this field; rebuilt on repair)   */
    uint8_t  reserved[44];   /**< Reserved for future use; zeroed            */
    uint32_t header_crc;     /**< CRC32 of bytes 0..59                       */
} robust_cfg_file_header_t;

/* Compile-time size check (requires C11). */
_Static_assert(sizeof(robust_cfg_file_header_t) == 64,
               "robust_cfg_file_header_t must be exactly 64 bytes");

/* ------------------------------------------------------------------ */
/* Record slot — exactly 300 bytes                                    */
/* ------------------------------------------------------------------ */

/** Maximum key length including null terminator (matches public header). */
#define FMT_KEY_MAX    32
/** Maximum value length including null terminator (matches public header). */
#define FMT_VALUE_MAX  256

/**
 * One record slot in the data region of the file.
 * record_crc covers bytes 0 through 295 (all fields except record_crc).
 */
typedef struct __attribute__((packed)) {
    uint8_t  state;                  /**< slot_state_t                     */
    uint8_t  reserved[3];            /**< Zeroed                           */
    uint32_t timestamp;              /**< Unix epoch seconds of last write */
    char     key[FMT_KEY_MAX];       /**< Null-terminated key string       */
    char     value[FMT_VALUE_MAX];   /**< Null-terminated value string     */
    uint32_t record_crc;             /**< CRC32 of bytes 0..295            */
} robust_cfg_record_t;

_Static_assert(sizeof(robust_cfg_record_t) == 300,
               "robust_cfg_record_t must be exactly 300 bytes");

/* ------------------------------------------------------------------ */
/* Offset helpers                                                      */
/* ------------------------------------------------------------------ */

/** Byte size of the file header region. */
#define FMT_HEADER_SIZE  ((off_t)sizeof(robust_cfg_file_header_t))

/** Byte size of one record slot. */
#define FMT_RECORD_SIZE  ((off_t)sizeof(robust_cfg_record_t))

/** File offset of slot @p idx (zero-based). */
#define FMT_SLOT_OFFSET(idx) \
    (FMT_HEADER_SIZE + (off_t)(idx) * FMT_RECORD_SIZE)

/** Total file size for a given capacity. */
#define FMT_FILE_SIZE(cap) \
    (FMT_HEADER_SIZE + (off_t)(cap) * FMT_RECORD_SIZE)

#endif /* ROBUST_CFG_FORMAT_H */
