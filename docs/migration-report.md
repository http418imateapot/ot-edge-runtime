# Migration report

Record of how `ot-edge-runtime` was assembled from four previously separate
repositories, what was changed in the process, and what was verified.

Migration date: 2026-07-28.

## 1. Source repositories and target directories

| Source repository | Target directory | Language | Original license |
|-------------------|------------------|----------|------------------|
| `http418imateapot/runc-edge-api` | `runtime/` | Python | MIT |
| `http418imateapot/plc-ebpf-autoscaler` | `autoscale/` | Python | MIT |
| `http418imateapot/robust-binary-config` | `edgeconf/core/` | C | MIT |
| `http418imateapot/robust-config-exchange` | `edgeconf/patterns/` | C | MIT |

Every tracked file from each source repository was carried over unchanged apart
from the edits listed in section 5.

## 2. How history was preserved

Each source repository was mirror-cloned, then rewritten with
[`git filter-repo`](https://github.com/newren/git-filter-repo) v2.47.0 so that
every commit in its history has its files under the target directory:

```bash
git clone --mirror https://github.com/http418imateapot/<repo>.git <repo>.git
git -C <repo>.git filter-repo --to-subdirectory-filter <target-dir> --force
```

The four rewritten histories were then joined into a fresh repository with
unrelated-history merges — no squashing, no rebasing, no dropped commits:

```bash
git init -b main
git commit --allow-empty -m "chore: initialize ot-edge-runtime monorepo"
git remote add <name> <path-to-rewritten-mirror>
git fetch <name>
git merge --allow-unrelated-histories --no-edit <name>/main
```

Because `--to-subdirectory-filter` moves the paths at every point in history,
`git log -- runtime/` (and equivalently for the other directories) walks all the
way back to that project's very first commit.

### Commit-count verification

| Source repository | Commits on source `main` | Commits reachable in monorepo | Match |
|-------------------|--------------------------|-------------------------------|-------|
| `runc-edge-api` | 17 | 17 | yes |
| `plc-ebpf-autoscaler` | 23 | 23 | yes |
| `robust-binary-config` | 20 | 20 | yes |
| `robust-config-exchange` | 8 | 8 | yes |
| **Total carried over** | **68** | **68** | yes |

The monorepo's `main` contains 68 source commits, one empty initialisation
commit, four merge commits, and the post-merge documentation/licensing commits.

Each source's earliest commit is reachable through its directory:

| Directory | Earliest commit | Date |
|-----------|-----------------|------|
| `runtime/` | `96bbaa7` | 2025-02-14 |
| `autoscale/` | `cde4dec` | 2025-02-15 |
| `edgeconf/core/` | `09c90c2` | 2025-02-10 |
| `edgeconf/patterns/` | `969372e` | 2025-02-09 |

### Branches

`main` of each source repository is fully merged into the monorepo's `main`.
Eight source branches contained commits that were **not** reachable from their
`main`; rather than lose them, each was carried over as a `legacy/*` branch.
They are archival refs and are intentionally not merged into `main`:

| Monorepo branch | Source repository | Source branch | Unique commits |
|-----------------|-------------------|---------------|----------------|
| `legacy/runtime/develop` | `runc-edge-api` | `develop` | 2 |
| `legacy/edgeconf-core/copilot-task-930386664` | `robust-binary-config` | `copilot/task-177605380-930386664-…` | 2 |
| `legacy/edgeconf-patterns/copilot-analyze-industrial-project` | `robust-config-exchange` | `copilot/analyze-industrial-project` | 2 |
| `legacy/autoscale/dependabot-github_actions-actions-checkout-7` | `plc-ebpf-autoscaler` | `dependabot/github_actions/actions/checkout-7` | 1 |
| `legacy/autoscale/dependabot-github_actions-actions-setup-python-7` | `plc-ebpf-autoscaler` | `dependabot/github_actions/actions/setup-python-7` | 1 |
| `legacy/autoscale/dependabot-github_actions-softprops-action-gh-release-3` | `plc-ebpf-autoscaler` | `dependabot/github_actions/softprops/action-gh-release-3` | 1 |
| `legacy/autoscale/dependabot-pip-build-1.5.0` | `plc-ebpf-autoscaler` | `dependabot/pip/build-1.5.0` | 1 |
| `legacy/autoscale/dependabot-pip-pytest-9.1.1` | `plc-ebpf-autoscaler` | `dependabot/pip/pytest-9.1.1` | 1 |

All other source branches were strictly contained in their `main` and therefore
carried no unique history. Because the `legacy/*` branches were rewritten by
`filter-repo` in the same pass as `main`, their commits are the same objects as
the ones on `main` where the histories overlap.

### One disclosed rewrite

The commits *created by this migration* (the initialisation commit, the four
merge commits and the documentation/licensing commits) were originally authored
with a personal e-mail address, which GitHub's e-mail privacy protection
rejected on push. Their author and committer e-mail was rewritten to
`177605380+http418imateapot@users.noreply.github.com`, matching the address the
source history already uses.

**No source commit was altered.** This was verified by confirming that the
pre-rewrite object IDs of all four sources' first and last commits still resolve
in the repository after the rewrite, and that the total commit count was
unchanged.

## 3. License change

All four sources were published under the MIT License by the same copyright
holder (`Copyright (c) 2025 Tinker`), and the four `LICENSE` files were byte-for-byte
identical. A single copyright holder may relicense their own work, so the
combined repository is distributed under the **Apache License 2.0**.

What was done:

- `LICENSE` at the repository root now carries the full Apache-2.0 text
  (retrieved from the GitHub licenses API), with the appendix copyright line
  filled in as `2025 Tinker (http418imateapot)`.
- `NOTICE` was added. It names the four source projects, their upstream URLs and
  their original MIT licensing, and reproduces the original MIT copyright notice
  in full — which the MIT terms require to be retained on redistribution.
- The four per-component `LICENSE` files were removed after their contents were
  consolidated into `NOTICE`. The repository now has exactly one `LICENSE`.
- Package metadata, `MANIFEST.in` entries and README badges were updated from
  MIT to Apache-2.0.

**No SPDX header changes were needed.** No source file in any of the four
projects carried a per-file MIT header, so there was nothing to convert. Headers
were not added, since adding them would have modified files the migration was
not otherwise touching.

Remaining occurrences of the string "MIT" in the repository are in
`autoscale/THIRD_PARTY_NOTICES.md`, where they describe the licenses of
third-party dependencies (PyYAML, pytest, build, setuptools, wheel). Those are
statements of fact about other projects and were correctly left alone.

## 4. What was deliberately not changed

No program logic, public API, CLI interface or configuration format was
modified. Specifically untouched: all `.py` and `.c`/`.h` sources, `config.json`,
the `config/*.example` files, `CMakeLists.txt`, both `Makefile`s, and the
`[project.scripts]` console-script entry points.

## 5. Path and metadata fixes

These were required because files moved into subdirectories, or because links
pointed at repositories that are being archived and made private.

### Packaging metadata

| File | Change | Why |
|------|--------|-----|
| `runtime/pyproject.toml` | `license = { file = "LICENSE" }` → `license = { text = "Apache-2.0" }` | The component directory no longer contains a `LICENSE` file; setuptools cannot reference one outside the project directory. |
| `runtime/pyproject.toml` | Classifier `License :: OSI Approved :: MIT License` → `… Apache Software License` | Relicensing. |
| `runtime/pyproject.toml` | 5 `[project.urls]` entries repointed to the monorepo and its `runtime/` paths | Old repository is going private. |
| `autoscale/pyproject.toml` | `license = "MIT"` → `license = "Apache-2.0"`; `license-files = ["LICENSE"]` removed | Relicensing; the referenced file no longer exists in the component directory. |
| `autoscale/pyproject.toml` | 5 `[project.urls]` entries repointed to the monorepo and its `autoscale/` paths | Old repository is going private. |
| `runtime/MANIFEST.in` | `include LICENSE` removed | File no longer present in the component directory. |
| `autoscale/MANIFEST.in` | `include LICENSE` removed | File no longer present in the component directory. |

### Documentation moves and links

Each source `README.md` was moved into `docs/` with `git mv`, so its rename is
tracked:

| From | To |
|------|----|
| `runtime/README.md` | `docs/runtime.md` |
| `autoscale/README.md` | `docs/autoscale.md` |
| `edgeconf/core/README.md` | `docs/edgeconf-core.md` |
| `edgeconf/patterns/README.md` | `docs/edgeconf-patterns.md` |

Relative links inside the moved documents were re-rooted so they resolve from
`docs/`:

| Document | Links re-rooted |
|----------|-----------------|
| `docs/runtime.md` | `.github/SDD.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, `VERSION` → `../runtime/…` |
| `docs/autoscale.md` | `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, `THIRD_PARTY_NOTICES.md` (×2), `config/adjust.env.example`, `config/machines.yaml.example`, `docs/PRODUCTION_DEPLOYMENT.md` (×3) → `../autoscale/…` |
| `docs/edgeconf-core.md` | `docs/SDD.md` (×2) → `../edgeconf/core/docs/SDD.md`; cross-reference to the `robust-config-exchange` repository → `edgeconf-patterns.md` |
| all four | `LICENSE` → `../LICENSE` |
| all four | `git clone <source repo>` → `git clone <monorepo>`, and the following `cd <old-repo-name>` → `cd ot-edge-runtime/<component-path>` |

Ten badge lines pointing at workflows and releases of the source repositories
were removed from the moved documents, because those workflows no longer run and
those release pages are going private: 2 from `docs/runtime.md`, 3 from
`docs/autoscale.md`, 3 from `docs/edgeconf-core.md`, 2 from
`docs/edgeconf-patterns.md`. The license and Python-version badges were kept.

A short `README.md` stub was written into each of the four component
directories. `runtime/` and `autoscale/` require one because their
`pyproject.toml` declares `README.md` as the package readme; the other two got
one for consistency. Each stub states what the component is and links to its
full document in `docs/`.

### Links to the archived source repositories

| File | Change |
|------|--------|
| `autoscale/CHANGELOG.md` | 2 release/compare links → monorepo commit history for `autoscale/` |
| `autoscale/CONTRIBUTING.md` | `git clone` URL → monorepo |
| `autoscale/SECURITY.md` | Security-advisory URL → monorepo |
| `autoscale/systemd/plc-adjust.service` | `Documentation=` URL → monorepo path |
| `autoscale/systemd/plc-decoder@.service` | `Documentation=` URL → monorepo path |
| `runtime/systemd/runc-edge-api.service` | `Documentation=` URL → monorepo |

The `systemd` changes touch only the `Documentation=` metadata field; no unit
behaviour and no configuration format was altered.

### New files

`LICENSE`, `NOTICE`, `README.md`, `README.zh-TW.md`, `docs/README.md`,
`docs/architecture.md`, this report, and `.github/workflows/ci.yml`.

### Diff size

From the post-merge state to the current `HEAD`: 31 files changed,
1778 insertions, 1027 deletions — the bulk being the added Apache-2.0 text, the
new top-level documents, and the README moves showing as delete+add pairs
alongside their renames.

## 6. Build and test verification

### Local (Windows 11, Python 3.12.7)

| Command | Working directory | Exit code | Result |
|---------|-------------------|-----------|--------|
| `pip install -e ".[dev]"` | `runtime/` | 0 | Editable install of `runc-edge-api` 2.0.0 succeeded |
| `python -m pytest` | `runtime/` | 0 | 42 passed |
| `pip install -e ".[dev]"` | `autoscale/` | 0 | Editable install of `plc-ebpf-autoscaler` 1.0.0 succeeded |
| `python -m pytest --basetemp=<dir>` | `autoscale/` | 0 | 22 passed |

The first `pytest` run in `autoscale/` reported 6 errors caused by a
`PermissionError` on the shared `%TEMP%\pytest-of-<user>` directory — an
environment permission issue, not a test failure. Re-running with an explicit
`--basetemp` gave 22 passed.

The C components could not be built locally: no C toolchain is available on the
Windows host, and the WSL Ubuntu-24.04 distribution has neither `gcc`, `cmake`
nor `make` and no passwordless `sudo` to install them. They were verified in CI
instead.

### CI (`ubuntu-latest`, run [30343673393](https://github.com/http418imateapot/ot-edge-runtime/actions/runs/30343673393))

All four jobs concluded `success`:

| Job | Commands | Result |
|-----|----------|--------|
| `runtime (runc-edge-api)` | `pip install -e ".[dev]"`, `python -m pytest` | 42 passed |
| `autoscale (plc-ebpf-autoscaler)` | `pip install -e ".[dev]"`, `python -m pytest` | 22 passed |
| `edgeconf/core (robust-binary-config)` | `make -C edgeconf/core`, `make -C edgeconf/core test` | Built `robustcfg`, `robust_cfg_tool` and 3 test binaries; ctest: 3/3 passed (`test_read_write`, `test_concurrent`, `test_fault_inject`) |
| `edgeconf/patterns (robust-config-exchange)` | `make -C edgeconf/patterns` | `gcc` built `robust_config` against the D-Bus back-end, no errors |

Test-suite inventory: `runtime/` and `autoscale/` shipped pytest suites and
`edgeconf/core/` shipped a ctest suite; all were run and all passed.
**`edgeconf/patterns/` has no test suite** in its source repository — none was
added, and its verification is limited to a successful compile.

The `ubus` back-end of `edgeconf/patterns` (`make IPC_BACKEND=ubus`) is **not**
exercised in CI: it needs `libubus`/`libubox`, which are OpenWrt packages not
available in the standard Ubuntu runner image.

## 7. Known limitations

- The components are not integrated into a shared build, a shared version
  number, or a shared release process. `.github/workflows/ci.yml` drives four
  independent builds; it does not unify them.
- The per-component CI/release workflows inherited from the source repositories
  now live at `runtime/.github/`, `autoscale/.github/`, `edgeconf/core/.github/`
  and `edgeconf/patterns/.github/`. GitHub only executes workflows in the
  repository-root `.github/workflows/`, so these are inert; they were kept
  because they are part of each source project's preserved content.
- `edgeconf/patterns/robust_config` is a prebuilt x86-64 ELF binary that was
  committed to the source repository. It was carried over as-is and is not
  reproducible from this repository's metadata; `make` overwrites it.
- Component documents in `docs/` remain in Traditional Chinese, as they were
  upstream. Only the top-level `README.md` is in English.
- Cross-component integration (the runtime actually driving autoscale's decoder
  instances, either Python component actually reading `edgeconf/core`) is
  described as the intended composition but is not implemented or tested here.
