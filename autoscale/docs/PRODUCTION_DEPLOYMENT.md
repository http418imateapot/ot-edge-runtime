# Production deployment guide

This guide defines the supported deployment profile for a Linux edge computer on a factory network. The MIT License permits commercial use, modification, redistribution, and private use subject to its notice requirements. It does not provide a warranty, indemnity, support SLA, or certification.

This service is an observability and data-pipeline component. Do not place it in a safety instrumented function, emergency-stop path, or direct closed-loop machinery control path. A site owner must complete its own cybersecurity, functional-safety, change-control, and acceptance process.

## Supported production profile

- Linux kernel 5.8 or newer with BTF/BCC support, systemd 246 or newer, and `CAP_BPF` plus `CAP_PERFMON`.
- Python 3.10 through 3.12.
- A dedicated `plcmon` service account without an interactive shell.
- An MQTT 3.1.1 broker reachable over authenticated TLS.
- Metrics bound to loopback or a protected management VLAN.
- Persistent storage sized and monitored for the SQLite processor and dead-letter queue.
- A reviewed module/unit configuration whose high-flow decoder count does not exceed the built-in limit of 256.

Older kernels that require `CAP_SYS_ADMIN` are outside the least-privilege profile shipped by this repository.

## Install an immutable release artifact

Use a reviewed release tag and verify the downloaded artifact checksum according to your organisation's software-supply-chain policy. The following layout allows BCC from the host package to remain available inside the virtual environment:

```bash
sudo apt-get update
sudo apt-get install -y python3 python3-venv python3-bpfcc bpfcc-tools linux-headers-$(uname -r)
sudo useradd --system --home /var/lib/plc-edgeflow --shell /usr/sbin/nologin plcmon
sudo install -d -o root -g root -m 0755 /opt/plc-edgeflow
sudo python3 -m venv --system-site-packages /opt/plc-edgeflow/venv
sudo /opt/plc-edgeflow/venv/bin/pip install --no-deps ./plc_ebpf_autoscaler-<version>-py3-none-any.whl
sudo /opt/plc-edgeflow/venv/bin/pip install paho-mqtt==2.1.0 PyYAML==6.0.3
```

Do not install the PyPI package named `bcc`; use the Linux distribution's BCC bindings.

## Configure credentials and TLS

```bash
sudo install -d -o root -g plcmon -m 0750 /etc/plc-edgeflow/{pki,secrets}
sudo install -o root -g plcmon -m 0640 config/adjust.env.example /etc/plc-edgeflow/adjust.env
sudo install -o root -g plcmon -m 0640 config/machines.yaml.example /etc/plc-edgeflow/machines.yaml
sudo install -o root -g plcmon -m 0640 factory-ca.pem /etc/plc-edgeflow/pki/factory-ca.pem
sudo install -o root -g plcmon -m 0640 mqtt-password /etc/plc-edgeflow/secrets/mqtt-password
```

The password is deliberately accepted only through a file. It is never placed in a process command line or emitted in logs. Keep `PLC_MQTT_TLS_INSECURE=false`; disabling hostname verification is only for isolated commissioning diagnostics.

Use a broker account restricted by ACL to the machine topic prefixes listed in `machines.yaml`. Prefer a unique credential per edge computer and rotate it using the site's secret-management process.

## Install and verify systemd

```bash
sudo install -o root -g root -m 0644 systemd/plc-adjust.service /etc/systemd/system/plc-adjust.service
sudo systemctl daemon-reload
sudo systemd-analyze verify /etc/systemd/system/plc-adjust.service
sudo systemctl enable --now plc-adjust.service
sudo systemctl status plc-adjust.service
curl --fail --silent http://127.0.0.1:9108/healthz
curl --fail --silent http://127.0.0.1:9108/metrics
```

The unit launches `plc-adjust` directly; there is no shell expansion of environment values. It requires the environment file and forces secure MQTT mode, so verified TLS plus username authentication or mutual TLS must be configured. The process reports systemd readiness only after at least one machine context attaches successfully.

## Factory acceptance checklist

- [ ] Record the approved release tag, artifact digest, kernel, BCC, Python, broker, and configuration revisions.
- [ ] Confirm the software bill of materials and `THIRD_PARTY_NOTICES.md` against local procurement policy.
- [ ] Validate MQTT server identity, client ACLs, credential rotation, and firewall rules.
- [ ] Verify that `/healthz` and `/metrics` are not reachable from the production-control VLAN unless explicitly required.
- [ ] Exercise PLC loss, broker loss, malformed messages, disk-full, process crash, SBC reboot, and configuration reload scenarios.
- [ ] Confirm decoder scaling limits against broker connection quotas and SBC CPU/memory capacity.
- [ ] Monitor dead-letter queue growth and establish retention/export procedures before production use.
- [ ] Confirm that failure of this service cannot inhibit machinery interlocks or safety functions.
- [ ] Complete the site's backup, rollback, incident-response, and maintenance-window approvals.

## Rollout and rollback

1. Deploy to a lab broker and replay representative traffic.
2. Run in `PLC_DRY_RUN=true` on one shadow edge computer and compare measured flow with the existing telemetry.
3. Enable processing for one non-critical machine and observe at least one normal production cycle.
4. Expand by a controlled batch only after health, resource, DLQ, and broker metrics remain within the approved limits.

To roll back, stop the service, reinstall the previously approved wheel, restore the matching configuration, run `systemctl daemon-reload`, and restart. SQLite schema changes must be reviewed for backward compatibility before every release; retain a storage backup made while the service is stopped.

## Operational signals

- Treat a non-zero service exit, systemd watchdog restart, `plc_context_health 0`, or growing `plc_ebpf_attach_errors_total` as an alert.
- Alert on repeated `plc_decoder_crashes_total` increases and dead-letter database growth.
- Retain structured logs according to site policy without forwarding MQTT credentials or raw sensitive payloads to third parties.
- Apply kernel and BCC updates in a staged maintenance window because probe availability can change between kernels.

## Support and certification boundary

Community support is provided through GitHub issues on a best-effort basis. Commercial users should establish their own support ownership, response targets, vulnerability intake, and long-term maintenance plan. The project does not claim IEC 62443, IEC 61508, ISO 13849, or site-specific certification.
