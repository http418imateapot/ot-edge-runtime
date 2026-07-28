# autoscale

eBPF-based PLC serial-port traffic monitor that dynamically scales MQTT decoder processes.

Watches PLC acquisition traffic in the kernel with eBPF and scales the number of decoder instances and the MQTT subscription strategy when the pipeline congests.

This directory is a component of the **ot-edge-runtime** monorepo. It was
previously published as the standalone `plc-ebpf-autoscaler` repository; its full commit
history is preserved here.

- Full component documentation: [`docs/autoscale.md`](../docs/autoscale.md)
- Monorepo overview and quick start: [`README.md`](../README.md)
- License: Apache-2.0 — see [`LICENSE`](../LICENSE) and [`NOTICE`](../NOTICE)
- Repository: https://github.com/http418imateapot/ot-edge-runtime
