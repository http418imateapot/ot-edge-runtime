# edgeconf/patterns

Fault-tolerant configuration sync daemon for POSIX processes that share settings through a config file.

Atomic writes, cross-process locking and inotify-driven delta broadcasting, so an OTA-delivered
setting takes effect immediately in every process without restarting any of them. The broadcast
channel sits behind a transport-neutral port (`include/ipc_backend.h`), with D-Bus shipping as the
reference adapter and ubus or a Unix socket addable without touching the daemon.

This directory is a component of the **ot-edge-runtime** monorepo. It was
previously published as the standalone `robust-config-exchange` repository; its full commit
history is preserved here.

- Full component documentation: [`docs/edgeconf-patterns.md`](../../docs/edgeconf-patterns.md)
- Factory scenarios this addresses: [`docs/scenarios.md`](../../docs/scenarios.md)
- Monorepo overview and quick start: [`README.md`](../../README.md)
- License: MIT — see [`LICENSE`](../../LICENSE)
- Repository: https://github.com/http418imateapot/ot-edge-runtime

```bash
make          # build with the D-Bus adapter (default)
make test     # unit tests, no bus required
```
