# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] — 2026-06-30

### Added
- **`librobustcfg` C library** with fully documented public API (`include/robust_cfg.h`)
  - `robust_cfg_open()` / `robust_cfg_close()` — open/create a config file with header validation
  - `robust_cfg_write()` / `robust_cfg_read()` / `robust_cfg_delete()` — core KV operations
  - `robust_cfg_repair()` — reset CORRUPT slots to EMPTY so space can be reclaimed
  - `robust_cfg_compact()` — remove DELETED/CORRUPT slots and rewrite the file
- **Structured binary format v1** (`src/robust_cfg_format.h`)
  - 64-byte file header with magic `"RCFG"`, format version, capacity, record count, and CRC32
  - 300-byte record slots with slot state, Unix timestamp, key[32], value[256], and CRC32
  - Log-structured writes: new slot → `fdatasync` → write-verify → old slot marked DELETED
- **CRC32 integrity protection** — header CRC and per-record CRC detect bit-flip and flash corruption
- **POSIX readers-writer locking** via `fcntl` byte-range locks
  - Multiple processes can read concurrently (`F_RDLCK` on entire file)
  - Writes are exclusively serialized (`F_WRLCK` on entire file)
  - Non-blocking `F_SETLK` + retry loop (500 × 10 ms) avoids indefinite blocking
- **Thread-safe handle** with `pthread_mutex_t` for intra-process concurrency
- **CLI tool** `robust_cfg_tool` supporting `read`, `write`, `delete`, `repair`, `compact`
- **Test suite** (`tests/`)
  - `test_read_write` — unit tests for all API functions and edge cases
  - `test_concurrent` — multi-process concurrent read/write validation
  - `test_fault_inject` — fault injection: simulates mid-write crash and verifies recovery
- **CMake build system** with `BUILD_TESTING`, `BUILD_SHARED_LIBS`, `INSTALL` targets and `pkg-config` support
- **GitHub Actions CI** (`ci.yml`)
  - Builds and runs tests on Ubuntu 22.04
  - AddressSanitizer + UndefinedBehaviorSanitizer passes
  - Cross-compilation for `aarch64-linux-gnu`
- **Software Design Document** (`docs/SDD.md`) with full format spec, concurrency model, and API contract
- **Copilot instructions** (`.github/copilot-instructions.md`) for AI-assisted development context

### Changed
- Migrated from `Makefile` (gcc direct) to `CMakeLists.txt`; `Makefile` kept as a convenience wrapper
- Key field expanded from 8 bytes → 32 bytes; value field expanded from 32 bytes → 256 bytes
- Config file path is now a runtime argument instead of a compile-time constant

### Removed
- Committed binary `robust_bin_config` removed from version control
- Original monolithic `src/robust_bin_config.c` replaced by `src/robust_cfg.c` + `src/cli.c`

### Fixed
- `F_SETLKW` infinite-block replaced with `F_SETLK` + timeout retry
- Missing `fsync` after writes (data could be lost on power loss)
- `atoi()` for index parsing replaced; CLI now uses key-based access with proper input validation
- No checksum / data integrity verification in original code

---

## [0.1.0] — initial prototype

- Single-file C program demonstrating `fcntl` byte-range locking concept
- Fixed-size packed struct (`key[8]`, `value[32]`) written directly to binary file
- Index-based read/write CLI interface
