# Architecture decision register

Status vocabulary: `PROPOSED`, `ACCEPTED_GATE_ZERO`, `SUPERSEDED`. Gate Zero
acceptance freezes decisions for Update 01; later changes require a new ADR,
impact analysis, migrations where relevant, and Warden review.

| ADR | Status | Decision | Rationale and consequences |
|---|---|---|---|
| ADR-0001 | ACCEPTED_GATE_ZERO | C17 authoritative core; C++20 orchestration; CMake ≥3.25 with Ninja reference build | Explicit portable standards, broad compiler support, strict language boundary |
| ADR-0002 | ACCEPTED_GATE_ZERO | Authoritative core is a library with opaque `fd_context`/`fd_world` handles; callers never address state fields | API stability, ownership clarity, no parallel client rule engines |
| ADR-0003 | ACCEPTED_GATE_ZERO | Caller supplies bounded configuration and optional allocator at creation; creation/generation may allocate, ordinary steps may not | Bounded memory and measurable step behavior |
| ADR-0004 | ACCEPTED_GATE_ZERO | Row-major rectangular tile arrays, origin northwest, `x` east, `y` south; 32-bit coordinates at API boundary | Canonical iteration and serialization order |
| ADR-0005 | ACCEPTED_GATE_ZERO | The region graph is a derived/indexed strategic view whose physical edges reference validated tile-world corridors; political-only edges carry a typed treaty relation | Prevents silent two-level contradictions |
| ADR-0006 | ACCEPTED_GATE_ZERO | Initial authoritative values use fixed-width integers; money is signed 64-bit minor units, quantities unsigned 32-bit units, ratios signed 32-bit parts-per-million | Deterministic math with explicit range and overflow tests |
| ADR-0007 | ACCEPTED_GATE_ZERO | Update order is the charter's 23-stage order. Each stage iterates stable numeric IDs ascending; changes are staged then committed | Removes container/thread timing dependence |
| ADR-0008 | ACCEPTED_GATE_ZERO | Counter-based Philox4x32-10 scopes keyed through SHA-256 domain derivation from seed, versions, and stable semantic IDs | Reproducible isolated samples independent of scheduling; policy details in `DETERMINISM_AND_RNG.md` |
| ADR-0009 | ACCEPTED_GATE_ZERO | Canonical state digest is SHA-256 over a versioned canonical byte stream, independent of save compression and in-memory padding | Cross-platform replay diagnosis and content integrity |
| ADR-0010 | ACCEPTED_GATE_ZERO | Saves are versioned sectioned binary containers; replays contain scenario identity, joint decisions, diagnostics, and periodic canonical hashes | Migration, unknown-section handling, deterministic divergence location |
| ADR-0011 | ACCEPTED_GATE_ZERO | Rendering consumes immutable snapshots/views and returns text through caller buffers; ASCII mode is normative and color/Unicode are presentation options | Headless purity and testable independent terminal design |
| ADR-0012 | ACCEPTED_GATE_ZERO | Hierarchical actions use typed category/verb/parameter stages plus exact masks; final submission is revalidated transactionally | Avoids flat action explosion and silent invalid-action behavior |
| ADR-0013 | ACCEPTED_GATE_ZERO | Public, private, inferred, and unknown information are separate C queries/snapshot sections, not a client-side filtering convention | Makes leakage testable at the authority boundary |
| ADR-0014 | ACCEPTED_GATE_ZERO | One world is stepped by one thread at a time; distinct worlds may run concurrently; no shared mutable RNG/cache | Clear C thread safety and scalable C++ batching |
| ADR-0015 | ACCEPTED_GATE_ZERO | Update 01 has no required runtime third-party library. Test-only dependencies must be pinned and license-recorded | Small reproducible core; dependencies added only for measured need |
| ADR-0016 | ACCEPTED_GATE_ZERO | Public ABI structs begin with `struct_size` and API/version queries; symbols use `fd_`; errors are return values plus caller-owned diagnostics | Forward-compatible opaque ABI without exceptions |
| ADR-0017 | ACCEPTED_GATE_ZERO | Save/replay compatibility is explicit: readers accept current and documented migrated older versions; newer major formats fail structurally | No silent reinterpretation or partial load |
| ADR-0018 | ACCEPTED_GATE_ZERO | Content uses validated data schemas and stable content IDs; executable mods are outside the initial trust boundary | Future modding without unbounded/unsafe authoritative input |
| ADR-0019 | ACCEPTED_GATE_ZERO | CPU is the only simulation backend. PPO tensor/GPU backend remains an explicit U47 architecture decision with CPU oracle | Obeys phase timing; CUDA availability creates no acceleration claim |
| ADR-0020 | ACCEPTED_GATE_ZERO | Thin browser and terminal clients submit actions and display authoritative state; neither computes rule outcomes | One rule engine, protocol-testable clients |
| ADR-0021 | ACCEPTED_GATE_ZERO | Historical/political mechanics require consequence variables and independent agency tests before acceptance | Makes the charter's ethical constraints mechanical and reviewable |
| ADR-0022 | ACCEPTED_GATE_ZERO | Reference mode is single-threaded, assertions enabled, extra validation/hash checks, and optimization-independent | Differential oracle for batching and future optimization |
| ADR-0023 | ACCEPTED_GATE_ZERO | Public IDs are 64-bit generation-tagged handles (32-bit slot, 32-bit generation); ID 0 is invalid; allocation reuses slots only with generation change | Detects stale references while keeping bounded tables |
| ADR-0024 | ACCEPTED_GATE_ZERO | Update specifications and evidence are versioned repository artifacts; no update enters implementation without named invariants and test/evidence plans | Prevents scaffold-only completion and undocumented rule drift |

## Deferred decisions with fixed decision points

These are not Gate Zero conflicts because the charter assigns them to later
phases. Deferral does not permit placeholder claims.

| Decision | Must be resolved before | Required evidence |
|---|---|---|
| PPO tensor library, GPU backend, precision, rollout layout, checkpoint tensor encoding | U47 `IMPLEMENTING` | toy-environment correctness and backend benchmark plan |
| Web transport and browser UI framework | U49 `IMPLEMENTING` | protocol threat model and no-rule-logic review |
| Optional Python binding technology | U50 `IMPLEMENTING` | ABI/lifetime tests and packaging reproduction |
| Final content schema language | first data-driven content update | bounds, validation, hash, migration, fuzz plan |
