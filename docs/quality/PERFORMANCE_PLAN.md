# Performance and benchmark plan

Status: Gate Zero plan. The initial simulation is CPU-first; no GPU performance
claim is permitted before the U47 architecture gate.

## Claim protocol

Every claim archives:

- commit and clean/dirty tree state;
- compiler, linker, standards, exact flags, build type;
- OS/kernel, CPU model, affinity/cgroup/quota/NUMA, RAM, GPU/driver only when used;
- live load, frequency/governor where visible, environment variables;
- ruleset/generator/config/content hashes and exact seed set;
- warm-up, at least 30 measured repetitions, raw per-repetition data;
- median, mean, standard deviation, p95, p99, and a confidence interval;
- baseline artifact and correctness/hash comparison.

Only the measured statistic is claimed. Best-run-only output is invalid. A tool
name or initialized CUDA context is not acceleration evidence.

## U01 benchmark workloads and budgets

`U01-default`: 48×24, one region, two settlements, one river, one road candidate,
one polity, two foreign factions. `U01-max`: 256×128 with the same required entity
counts and maximum configured terrain complexity. Generation uses the fixed
1,000-seed benchmark set `0..999`; replay stepping uses 1,024 generated worlds.

| Metric | Workload | Gate budget on baseline-class uncontended x86-64 CPU |
|---|---|---:|
| World generation | default | p95 ≤10 ms |
| World generation | maximum | p95 ≤100 ms |
| Authoritative live state | default | ≤256 KiB excluding context/code |
| Authoritative live state | maximum | ≤4 MiB excluding context/code |
| Canonical save | default | ≤256 KiB |
| Canonical save | maximum | ≤4 MiB |
| Ordinary reference step allocations | either | exactly 0 |
| PASS decision intervals | default scalar | ≥10,000/s |
| 64-world batch, 8 pinned physical cores | default | ≥50,000 intervals/s |
| Canonical serialization/load | aggregate ≥64 MiB | each ≥100 MiB/s |
| Canonical state hash | default | p95 ≤250 µs |
| ASCII render | 120×40 | p95 ≤1 ms |
| Replay record excluding checkpoint | one PASS interval | average ≤2 KiB |

These budgets are acceptance targets, not current results. Failure is recorded
and profiled; targets change only by an ADR with old/new values, raw evidence,
correctness impact, and Warden approval.

## Full VS1 reference workload

Before the VS1 gate (U28), freeze a versioned 128×64 campaign benchmark with the
local polity, two expeditions, bounded maximum 4,096 live entities, 256 transport
groups, campaign horizon, scripted policies, and content hash. It must cover all
charter metrics: complete games, pathfinding, supply, economy, masks,
observations, serialization, replay, memory, and thread scaling. Numeric budgets
are fixed no later than the first owning update, never after seeing final results.

U40 and U41 define four/eight-seat maximum-world configurations and hard state,
RSS, step, and complete-game budgets before their implementation claims.

U47 separately decides tensor/GPU backend, device ownership, precision, batch
layout, transfers, rollout storage, scheduling, checkpoints, CPU fallback, and
determinism. GPU utilization and learner/inference throughput are reported only
for an actually used device backend with end-to-end comparison.

## Benchmark implementation rules

- Rendering is disabled except the renderer benchmark.
- Canonical hashing is enabled unless the metric name explicitly isolates hash
  cost; both variants may be reported but never conflated.
- Scalar work pins one physical core; batch work records exact pinned cores and
  thread count. Hyperthreads are identified, not assumed.
- Warm-up covers caches and allocator setup but does not mutate measured initial
  worlds unexpectedly.
- Timed setup/generation/step/render/storage phases are separate.
- Peak memory uses a named tool/method and separates code/shared libraries,
  context, per-world state, rollout, and output buffers where possible.
- Optimized and reference runs compare final canonical hashes for every seed.
- Profile first. SIMD, incremental hash/cache, custom allocator, GPU, or custom
  kernel work requires a measured bottleneck and a correct reference path.

## Baseline environment caveat

The initial shell exposed 128 logical processors and 125 GiB, while the live Vast
capability API reported 61 CPUs and about 64 GB. The workspace is an overlay and
not persistent. Actual benchmark manifests must capture affinity/quota and portal
state at run time; host totals in Gate Zero are not valid result provenance.

