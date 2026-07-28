# Changelog

All notable changes to **plc-ebpf-autoscaler** are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).  
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added

- MQTT username/password-file authentication, verified TLS, and optional mutual TLS for adjuster-managed and standalone decoders.
- Production deployment, rollout, rollback, factory acceptance, and third-party licensing documentation.
- Secure environment and multi-machine configuration examples.
- Deployment contract tests for secure systemd and MQTT defaults.

### Changed

- systemd units now execute installed console scripts directly without a shell and use a stronger least-privilege sandbox.
- Runtime requirements no longer include the host-provided BCC bindings or development-only pytest dependency.
- Runtime dependencies now use Paho MQTT 2.1 callback API VERSION2 and PyYAML 6.0.3.
- Release artifacts now include SHA-256 checksums, and Dependabot tracks Python and GitHub Actions updates.
- Initial configuration or eBPF attach failure now prevents the adjuster from reporting systemd readiness.

---

## [1.0.0] — 2026-06-30

First stable release. All P0 / P1 / P2 hardening tasks completed and verified on target hardware (SBC, Linux 5.x, Mosquitto 2.x).

### Added

#### P0 — Correctness
- **TASK-01** Fix MQTT topic wildcard format — all subscription topics are now valid MQTT 3.1.1 filter strings; the multi-level wildcard `#` is always the final segment.
- **TASK-02** Replace `tty_read` call-count kprobe with a **kretprobe** that reads the actual bytes returned via `PT_REGS_RC(ctx)`, giving an accurate bytes-per-interval measurement.
- **TASK-03** Parameterise machine serial number — `adjust.py` now requires `--machine_sn` (or the `PLC_MACHINE_SN` environment variable); it is never hardcoded.

#### P1 — Process Lifecycle Management
- **TASK-04** Track spawned decoder subprocesses in `active_decoders: dict[str, Popen]`; `reconcile_decoders()` is the single place that spawns or terminates processes. Scale-in (topic removal) sends `SIGTERM` then `SIGKILL` after a 5-second timeout.
- **TASK-05** Prevent duplicate decoder spawning — `reconcile_decoders()` checks `active_decoders` before spawning and `proc.poll()` to detect crashes; dead processes are restarted on the next reconcile cycle.
- **TASK-06** Add MQTT reconnect logic to `decoder.py` — exponential back-off retry (initial 2 s, max 60 s) implemented in `on_disconnect`; re-subscribes in `on_connect` using topic from `userdata`.

#### P2 — Observability & Deployment
- **TASK-07** Structured JSON logging in both `adjust.py` and `decoder.py` via the `JsonFormatter` class; every log line contains `ts`, `level`, and `event` keys plus domain-specific extra fields.
- **TASK-08** systemd unit files — `systemd/plc-adjust.service` (Type=notify, watchdog, AmbientCapabilities) and `systemd/plc-decoder@.service` (template unit, BindsTo adjuster).
- **TASK-09** Remove `time.sleep()` simulation stub from `decoder.py`; processing path executes without artificial delay.
- **TASK-10** Pin all dependency versions in `requirements.txt` (`paho-mqtt==1.6.1`, `bcc==0.29.1`, `pytest==8.3.5`, `PyYAML==6.0.2`).

### Also Added (post P2 enhancements)
- Multi-machine YAML configuration file with hot-reload via `SIGHUP` (`--config`).
- HTTP `/healthz` and `/metrics` (Prometheus format) endpoints served by `adjust.py`.
- Dead-letter queue (SQLite WAL) in `decoder.py` for messages that fail processing.
- `SQLiteProcessor` plugin in `decoder.py` for direct point persistence.
- Crash-rate threshold and restart back-off for decoder subprocesses.
- `pyproject.toml` — `pip install .` support, console scripts `plc-adjust` / `plc-decoder`.

---

[Unreleased]: https://github.com/http418imateapot/ot-edge-runtime/commits/main/autoscale
[1.0.0]: https://github.com/http418imateapot/ot-edge-runtime/commits/main/autoscale
