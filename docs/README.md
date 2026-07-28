# Documentation index

| Document | Contents |
|----------|----------|
| [`scenarios.md`](scenarios.md) · [`scenarios.zh-TW.md`](scenarios.zh-TW.md) | The four plant-floor situations this toolkit was built for: symptom, why the usual answer does not fit, what this project does instead, and a worked example for each. Start here if you want to know *why* before *how*. |
| [`architecture.md`](architecture.md) | How the four components fit together, with a component map (Mermaid) and the deliberate non-goals. |
| [`runtime.md`](runtime.md) | `runtime/` — runc/cgroups container management REST API. Originally the `runc-edge-api` repository. |
| [`autoscale.md`](autoscale.md) | `autoscale/` — eBPF PLC traffic monitor and decoder autoscaler. Originally the `plc-ebpf-autoscaler` repository. |
| [`edgeconf-core.md`](edgeconf-core.md) | `edgeconf/core/` — embedded binary key-value configuration engine. Originally the `robust-binary-config` repository. |
| [`edgeconf-patterns.md`](edgeconf-patterns.md) | `edgeconf/patterns/` — fault-tolerant config sync daemon and its replaceable IPC transport layer. Originally the `robust-config-exchange` repository. |
| [`migration-report.md`](migration-report.md) | Record of the monorepo merge: source-to-directory mapping, history preservation method, every path fix applied, and the license change. |

Component-specific material that was not part of a source README stayed with
its component:

- [`../runtime/.github/SDD.md`](../runtime/.github/SDD.md) — runtime software design description
- [`../autoscale/docs/PRODUCTION_DEPLOYMENT.md`](../autoscale/docs/PRODUCTION_DEPLOYMENT.md) — production deployment, acceptance and rollback guide
- [`../autoscale/THIRD_PARTY_NOTICES.md`](../autoscale/THIRD_PARTY_NOTICES.md) — autoscale dependency license inventory
- [`../edgeconf/core/docs/SDD.md`](../edgeconf/core/docs/SDD.md) — edgeconf core software design description

Each component also keeps its own `CHANGELOG.md`, `CONTRIBUTING.md` and
`SECURITY.md` where the source repository had them.

The four component documents are written in Traditional Chinese, as they were in
the source repositories; they were moved here verbatim apart from link and
license corrections.
