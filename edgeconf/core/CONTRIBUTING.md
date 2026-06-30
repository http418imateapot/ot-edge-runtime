# Contributing to robust-binary-config

Thank you for your interest in contributing! This document explains how to get started.

---

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How to Contribute](#how-to-contribute)
- [Development Environment](#development-environment)
- [Coding Conventions](#coding-conventions)
- [Tests](#tests)
- [Submitting Changes](#submitting-changes)
- [Release Process](#release-process)

---

## Code of Conduct

Be respectful, constructive, and collaborative. Harassment or abuse of any kind will not be tolerated.

---

## How to Contribute

| Type | How |
|------|-----|
| Bug report | Open a GitHub Issue with a minimal reproducer |
| Feature request | Open a GitHub Issue describing the use-case |
| Security vulnerability | See [SECURITY.md](SECURITY.md) — **do not** use public issues |
| Code patch | Fork → branch → pull request (see below) |
| Documentation | Same PR flow as code |

---

## Development Environment

### Prerequisites

```bash
sudo apt-get install build-essential cmake
```

For cross-compilation to `aarch64` (Raspberry Pi / i.MX8):

```bash
sudo apt-get install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

### Build

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build --parallel
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

### Sanitizer build (required before submitting a patch)

```bash
cmake -B build-asan \
  -DBUILD_TESTING=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

---

## Coding Conventions

- **Language**: C11 (`-std=c11`), no compiler extensions (`CMAKE_C_EXTENSIONS OFF`)
- **Formatting**: K&R-style braces, 4-space indent
- **Naming**:
  - Public API: `robust_cfg_<noun>_<verb>()` prefix
  - Internal helpers: `static` functions, no prefix required
- **Error handling**: Return negative error codes; success returns `ROBUST_CFG_OK` (0)
- **Comments**: Doxygen-style `/** ... */` on public API in `include/robust_cfg.h`; inline comments for non-obvious logic
- **No dynamic external dependencies**: CRC32 is implemented internally; do not add third-party libraries
- **Write safety**: Every write path must call `fdatasync` and perform a write-verify (read-back CRC check)

---

## Tests

All patches that change library behaviour must include or update tests under `tests/`.

| Test file | Purpose |
|-----------|---------|
| `test_read_write.c` | Unit tests for all API functions and edge cases |
| `test_concurrent.c` | Multi-process concurrent read/write validation |
| `test_fault_inject.c` | Fault injection: mid-write crash recovery |

A CI run with AddressSanitizer + UBSanitizer must pass before a PR is merged.

---

## Submitting Changes

1. Fork the repository and create a feature branch from `main`:
   ```bash
   git checkout -b feat/my-feature
   ```

2. Make your changes, keeping commits atomic and with clear messages:
   ```
   fix: handle empty key in robust_cfg_write()
   feat: add robust_cfg_iterate() for key enumeration
   docs: update SDD.md with new format version notes
   ```

3. Ensure tests pass (including the sanitizer build above).

4. Update `CHANGELOG.md` under `## [Unreleased]` following the [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) format.

5. Open a pull request against `main`. Fill in the PR template and link any relevant issues.

---

## Release Process

Releases are managed by maintainers:

1. Update `CHANGELOG.md`: rename `[Unreleased]` to `[X.Y.Z] — YYYY-MM-DD`.
2. Update `VERSION` and `CMakeLists.txt` `project(... VERSION X.Y.Z ...)`.
3. Update `ROBUST_CFG_VERSION_*` macros in `include/robust_cfg.h`.
4. Commit as `chore: release vX.Y.Z` and tag `vX.Y.Z`.
5. Push the tag — the release workflow will build and attach CPack artifacts automatically.
