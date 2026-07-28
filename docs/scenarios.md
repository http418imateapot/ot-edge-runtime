# Factory scenarios

繁體中文版本：[scenarios.zh-TW.md](scenarios.zh-TW.md)

This document describes the plant-floor situations `ot-edge-runtime` was built
for: what goes wrong, why the usual answers do not fit, what this project does
instead, and a worked example for each. If you want the component-by-component
view instead, start from [`architecture.md`](architecture.md).

## The setting

A single industrial single-board computer — a fanless box on a DIN rail, or a
Jetson/i.MX8-class module inside a machine cabinet. It sits between OT and IT:

- **Below it**, a PLC or a set of sensors, usually over a serial line.
- **Above it**, the plant network: an MQTT broker, a historian, an MES.
- **Inside it**, several long-running processes that must not interfere with
  each other, on storage that will lose power without warning.

Two constraints shape everything:

1. **Resources are small and fixed.** There is no scaling out. A control plane
   that costs a few hundred MB of resident memory is a control plane you cannot
   afford.
2. **Nobody is going to log in.** The box has to survive power cuts, network
   outages and a misbehaving workload without a human on site.

---

## Scenario 1 — Acquisition backs up during a high-frequency run

**On the floor.** A semiconductor metrology step runs at high sampling rate. A
PLC pushes point data down a serial line; a decoder process on the SBC parses it
and forwards it to an MQTT topic for the historian.

**The symptom.** During a burst, the decoder cannot keep up. The serial buffer
fills, points are dropped, and the loss shows up in the historian as gaps — not
as an alarm. Worse, it is intermittent: it only happens on the fastest recipes,
so it does not reproduce on the bench.

**Why the usual answers fall short.** Polling `/proc` or the application's own
counters tells you about congestion *after* the queue has already built. Sizing
the decoder pool for peak load wastes memory the box does not have. And a fixed
pool cannot react to a recipe change at all.

**What this project does.** [`autoscale/`](../autoscale/) watches the serial
ingress **in the kernel with eBPF**, so the traffic signal arrives before
userspace queues build. A controller compares that signal against per-machine
thresholds and scales the number of decoder instances — and the MQTT
subscription strategy — up or down to match.

**Worked example.**

```bash
# Describe the machines and their thresholds, and point the controller at them.
# The unit reads site overrides from /etc/plc-edgeflow/adjust.env; PLC_CONFIG
# there names the YAML file (or pass --config on the command line).
cp autoscale/config/machines.yaml.example /etc/plc-edgeflow/machines.yaml
cp autoscale/config/adjust.env.example     /etc/plc-edgeflow/adjust.env

# The controller and the decoder instances run as systemd units;
# plc-decoder@.service is a template, instantiated once per decoder.
systemctl enable --now plc-adjust.service
systemctl status 'plc-decoder@*.service'
```

The controller starts and stops `plc-decoder@N` instances as the observed rate
crosses the configured thresholds. Details, including the production hardening
checklist (TLS, least privilege, rollout and rollback), are in
[`autoscale.md`](autoscale.md) and
[`../autoscale/docs/PRODUCTION_DEPLOYMENT.md`](../autoscale/docs/PRODUCTION_DEPLOYMENT.md).

**Limits.** The eBPF probe is kernel-version sensitive and needs the BCC
bindings from the system package manager. It observes; it does not throttle the
PLC.

---

## Scenario 2 — One workload starves the one that matters

**On the floor.** The same box now runs the acquisition path *and* something
else: an inference process, a vendor's uploader, a diagnostic tool someone
installed during commissioning.

**The symptom.** The uploader hits a retry storm during a network blip, eats
CPU and memory, and the acquisition path — the one process that must never
stall — starts missing its deadline. Nothing crashed, so nothing alerted.

**Why the usual answers fall short.** Bare `systemd` will restart a process that
dies, but it gives you no enforced ceiling between the workload that matters and
the one that does not, no image lifecycle, and no API an operator tool can
drive. K3s or KubeEdge would give you all three — along with a cluster control
plane, an embedded datastore and a CNI, on a box whose entire job is to decode
serial data.

**What this project does.** [`runtime/`](../runtime/) uses `runc` and cgroups
**directly**: each workload runs in an OCI container with an explicit resource
ceiling, isolated in a user namespace, driven over an authenticated REST API
rather than a cluster control plane. There is no scheduler and no cluster
membership, because there is one node.

**Worked example.**

```bash
# The API service runs under systemd, bound to loopback on port 8000.
# API_KEY comes from the EnvironmentFile; every /api/* call needs it.
systemctl enable --now runc-edge-api.service

# Start a workload
curl -sS -X POST http://127.0.0.1:8000/api/containers/start \
  -H "X-API-Key: $API_KEY" \
  -H 'Content-Type: application/json' \
  -d '{"container_id":"uploader"}'

# Tighten the ceiling on the running container — `runc update` under the hood,
# so it takes effect without a restart. memory_limit is in bytes;
# cpu_shares is a relative weight, not a hard quota.
curl -sS -X PATCH http://127.0.0.1:8000/api/containers/uploader/resources \
  -H "X-API-Key: $API_KEY" \
  -H 'Content-Type: application/json' \
  -d '{"cpu_shares":512,"memory_limit":268435456}'
```

The full endpoint set (`/health`, `/api/containers`, `/api/containers/{id}`,
start, stop, resources), the OCI spec in `config.json`, and the subuid/subgid
setup that user namespaces require are documented in [`runtime.md`](runtime.md).

**Limits.** Single node only. No image registry, no overlay networking, no
scheduler. You supply the root filesystem.

---

## Scenario 3 — The box loses power mid-write and comes back wrong

**On the floor.** Plant power drops — a breaker, a UPS that did not hold, or
simply someone pulling the cabinet. The SBC is writing its configuration at that
moment, to eMMC or an SD card.

**The symptom.** The box comes back with a truncated or half-written config
file. Depending on the format, the process either refuses to start or, worse,
starts with a partially-parsed configuration and behaves subtly wrongly.

**Why the usual answers fall short.** A plain text or INI file has no atomicity
story at all; a partial write is a corrupt file. SQLite solves it properly, but
brings a WAL, a page cache and a dependency that is heavy for storing forty
key/value pairs on a device with tight flash and tight RAM.

**What this project does.** [`edgeconf/core/`](../edgeconf/core/) is a small C
key/value store sized for exactly this gap — deliberately between "SQLite is too
heavy" and "a text file is too fragile". It ships as a library
(`librobustcfg`) plus a CLI (`robust_cfg_tool`), and its test suite includes a
fault-injection case specifically for torn writes.

**Worked example.**

```bash
make -C edgeconf/core          # builds librobustcfg + robust_cfg_tool
make -C edgeconf/core test     # includes test_fault_inject and test_concurrent
```

Link against it from an application with the shipped pkg-config file, or drive
it from a shell with `robust_cfg_tool`. See [`edgeconf-core.md`](edgeconf-core.md).

**Limits.** No WAL: atomic update of several keys as one transaction is left to
the application. It is a key/value store, not a database.

---

## Scenario 4 — A new setting has to take effect without restarting anything

**On the floor.** An OTA push changes a sampling rate or a threshold. Five
processes on the box care about that value: the acquisition loop, the decoder,
the uploader, a watchdog, a local UI.

**The symptom.** Restarting all five to pick up one number means a gap in
acquisition — during production. So instead each process polls the file, which
costs CPU and still races: two processes doing read-modify-write on the same
file will overwrite each other, and a reader can catch the file mid-write.

**Why the usual answers fall short.** Polling is both slow to react and wasteful.
Ad-hoc file locking tends to be forgotten in one code path out of five. And a
notification mechanism hard-wired to D-Bus does not exist on an OpenWrt or
uClinux-class device, which has ubus instead — or on a minimal image, which has
neither.

**What this project does.** [`edgeconf/patterns/`](../edgeconf/patterns/) is a
sync daemon that makes the config file safe to share:

- **Atomic writes, and durable ones** — write to `.tmp`, `fsync`, then
  `rename()`, then `fsync` the directory. The rename alone is atomic against
  other readers but not against power loss; the fsyncs are what stop the box
  from booting to an empty config.
- **Cross-process mutual exclusion** — a `.lock` file with `flock(LOCK_EX)`
  around the whole read-modify-write cycle.
- **Delta broadcast** — `inotify` detects the change, the daemon diffs the
  before/after snapshot and publishes one event *per changed key* — including
  keys that were deleted, so a subscriber stops serving a retired value instead
  of keeping it forever. Listeners react in milliseconds without polling.
- **A replaceable transport** — the broadcast goes through the port in
  `include/ipc_backend.h`. D-Bus is the reference adapter that ships; ubus or a
  Unix socket can be added without the daemon knowing.
- **Trustworthy origin** — the daemon owns a well-known bus name and subscribers
  filter on it, so an unprivileged local process cannot forge a config change.

**Worked example.**

```bash
# Terminal 1 — the daemon watches the file and broadcasts deltas
./robust_config --ipc-address session --log-stderr watch

# Terminal 2 — anything that wants to react subscribes
./robust_config --ipc-address session --log-stderr dashboard

# Terminal 3 — the OTA push, or an operator, changes one key
./robust_config --log-stderr write --key sample_rate --value 120
```

Terminal 2 prints, within milliseconds and with no restart anywhere:

```
ConfigChanged: key=sample_rate value=120
```

Delete that key from the file instead, and every subscriber is told so rather
than being left with a stale value:

```
ConfigChanged: key=sample_rate removed (was 120)
```

The full design, the transport contract and how to add an adapter are in
[`edgeconf-patterns.md`](edgeconf-patterns.md).

**Limits.** The daemon distributes and protects the file; it does not decide
what the values mean. Only the D-Bus adapter is implemented today.

---

## Putting them together

The four components were written separately and each stands alone, but the
composition they were designed for is:

1. `runtime/` gives every workload a resource ceiling, so Scenario 2 cannot take
   down Scenario 1.
2. `autoscale/` decides how many decoders should exist from a kernel-level
   signal, and asks the runtime to make it so.
3. `edgeconf/core/` holds the thresholds and settings both of them read, on
   storage that will lose power.
4. `edgeconf/patterns/` distributes a change to those settings to every process
   at once, without a restart.

Adopting one does not require adopting the others. There is no cross-component
integration test in this repository — the composition above is the intended
design, not a verified end-to-end pipeline.

## What this project is not

- **Not a safety system.** Nothing here is certified against IEC 62443,
  IEC 61508 or ISO 13849. It is a monitoring and data-path toolkit, not
  safety-instrumented control, emergency stop, or closed-loop machine control.
  Apache-2.0 grants no warranty and no indemnity; validating a deployment
  remains the integrator's responsibility.
- **Not an orchestrator.** One node, no scheduler, no cluster.
- **Not portable beyond Linux**, and in places kernel-version sensitive.
