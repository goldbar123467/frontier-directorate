# Risk register

Scale: likelihood (`L`) and impact (`I`) are 1–5. Score is `L×I`. Owners are
roles, not individual authors. Open risks are reviewed at every update gate.

| ID | Risk | L | I | Score | Mitigation / trigger | Owner |
|---|---|---:|---:|---:|---|---|
| R-001 | Non-persistent workspace is lost on recycle/destroy | 4 | 5 | 20 | Initialize Git; establish off-box remote before irreplaceable implementation; record hashes. Trigger: first production commit | Program director |
| R-002 | Empty baseline encourages scaffold-only “completion” | 4 | 5 | 20 | Per-update behavioral acceptance/evidence; Warden rejects placeholders | Warden |
| R-003 | Cross-platform integer, endian, or padding drift | 3 | 5 | 15 | Fixed-width canonical encoding; GCC/Clang differential hashes; no struct dumps | C core / serialization |
| R-004 | RNG draw coupling changes worlds when subsystems evolve | 4 | 4 | 16 | Named independent streams, draw budgets, golden manifests, serialized counters | World generation |
| R-005 | Hydrology constraints make bounded generation fail frequently | 3 | 4 | 12 | Bounded attempts with constructive fallback and seed/property campaigns | Terrain/hydrology |
| R-006 | Initial objectives are unreachable or unfair | 3 | 5 | 15 | Reachability oracle, symmetric eligibility not symmetric geography, fairness bounds and rejection reasons | Scenario / Warden |
| R-007 | Two authoritative rule implementations emerge in C++/clients | 3 | 5 | 15 | Thin views, C API outcome authority, differential review | C API / Warden |
| R-008 | Ordinary steps allocate or scale with unbounded content | 3 | 4 | 12 | Creation-time capacities, allocation counters, fuzzed validators | Performance / security |
| R-009 | Arithmetic overflow corrupts economies or populations | 4 | 5 | 20 | Checked operations, limits in schemas, boundary property tests, UBSan | C economy / Warden |
| R-010 | Observation leaks hidden/future/RNG/debug information | 4 | 5 | 20 | Authority-separated views, metamorphic leakage suite, schema review | RL / intelligence |
| R-011 | Invalid actions partially mutate state | 3 | 5 | 15 | Validate-plan-commit architecture, before/after hash properties | C API / test |
| R-012 | Parallel batching introduces nondeterminism/races | 3 | 5 | 15 | Single-writer worlds, stable joint commit, TSan, schedule perturbation tests | C++ runtime |
| R-013 | Performance work changes rules | 3 | 5 | 15 | Reference mode and state-hash differential gate for every optimization | Performance / Warden |
| R-014 | Ethical constraints remain flavor text rather than mechanics | 3 | 5 | 15 | Consequence invariants, independent polity objectives, adversarial scenario review | Design / Warden |
| R-015 | Historical inspiration slips into proprietary copying | 2 | 5 | 10 | Asset-free ASCII, provenance log, license and similarity review | Release / legal |
| R-016 | Save/replay migrations silently discard state | 3 | 5 | 15 | Mandatory section handling, migration fixtures, round-trip and old-version tests | Serialization |
| R-017 | Evidence becomes self-reported and non-reproducible | 4 | 5 | 20 | Raw artifacts, commands, hashes, independent reruns, honest skipped fields | Warden |
| R-018 | GPU/toolkit architecture mismatch in PPO phase | 2 | 4 | 8 | Defer to U47; require CUDA ≥12.8 wheel on cc12.0 here and CPU oracle | PPO implementation |
| R-019 | Linear update chain increases schedule latency | 4 | 3 | 12 | Parallelize specification/test design within the active update, not verification dependencies | Program director |
| R-020 | Missing static-analysis tools produce false green gate | 4 | 3 | 12 | Record NOT RUN; install/pin before first applicable gate; never waive silently | Quality |
| R-021 | Region and tile representations diverge | 3 | 5 | 15 | Typed graph evidence back to tiles/treaties; invariant validation each step | C core |
| R-022 | Content or save inputs cause memory denial of service | 3 | 5 | 15 | Length/capacity validation before allocation, fuzzing, file size limits | Security |
| R-023 | C ABI lifetime misuse creates UAF or leaks | 3 | 5 | 15 | Explicit ownership table, destroy idempotence policy, sanitizer tests, RAII wrapper | C API |
| R-024 | Benchmarks exploit host totals despite quotas/affinity | 4 | 3 | 12 | Capture portal manifest, affinity, `lscpu`, clocks/load, repeats/raw data | Performance |

## Gate Zero disposition

No risk is “accepted away.” R-001 and R-020 require concrete follow-up before
their respective triggers. Neither is an external blocker to writing and
reviewing Gate Zero specifications; R-001 becomes an implementation blocker if
no recoverable repository copy exists before significant production work.

