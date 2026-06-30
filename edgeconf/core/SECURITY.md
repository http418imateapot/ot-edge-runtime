# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.x     | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

Please **do not** report security vulnerabilities through public GitHub issues.

Instead, open a [GitHub Security Advisory](https://github.com/http418imateapot/robust-binary-config/security/advisories/new) (private disclosure) so that we can coordinate a fix before public disclosure.

### What to include

- A clear description of the vulnerability and its potential impact.
- Steps to reproduce or a minimal proof-of-concept.
- Affected version(s) and platform(s).
- Any suggested mitigations or patches (optional but appreciated).

### Response timeline

| Milestone                        | Target         |
| -------------------------------- | -------------- |
| Acknowledgement of report        | ≤ 3 business days |
| Initial assessment               | ≤ 7 business days |
| Patch / mitigation               | ≤ 30 calendar days (severity-dependent) |
| Public disclosure (CVE if applicable) | Coordinated with reporter |

### Scope

This library runs on Linux edge devices and uses POSIX file I/O. Areas of particular interest include:

- CRC bypass or collision leading to silent data corruption
- Buffer overflows in key/value handling
- Lock-related race conditions or privilege escalation via advisory locking
- Arbitrary file write via `compact` or `repair` operations

### Out of scope

- Issues in downstream applications that use this library but are unrelated to the library's own code
- Known limitations documented in `README.md` (e.g. advisory-lock bypass by non-cooperative processes)

## Security Considerations for Users

- The `robust_cfg_open()` call should be passed a **trusted, application-controlled path**. Do not pass attacker-controlled paths.
- File permissions on the config file must be restricted to the owning process/user (e.g. `chmod 600`).
- `fcntl` advisory locks do **not** prevent access from root or processes that do not cooperate with the locking protocol.
