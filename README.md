# Frontier Directorate

> **Status: In progress.** Update 01 is implemented locally but remains
> unverified until its committed evidence passes independent Warden review.

Frontier Directorate is a terminal-first procedural grand-strategy logistics
game and multi-agent reinforcement-learning research environment. The
authoritative simulation is written in C; C++ owns orchestration, clients, and
learning systems.

Gate Zero passed independent review on 2026-07-29. Update 01, the procedural
coastal corridor bootstrap, is currently **IMPLEMENTING**; it is not `VERIFIED`
until its exact committed tree and raw evidence pass an independent Warden
review. Later updates remain intentionally unspecified until their dependency
and specification gates open.

Start with:

- `docs/evidence/GATE_ZERO_EVIDENCE.md`
- `docs/requirements/REQUIREMENT_TRACEABILITY.md`
- `docs/requirements/ATOMIC_REQUIREMENT_CATALOG.md`
- `docs/requirements/NORMATIVE_PROSE_CATALOG.md`
- `docs/updates/UPDATE_REGISTRY.md`
- `docs/specs/INITIAL_VERTICAL_SLICE.md`

## Update 01 implementation

The U01 implementation provides:

- a strict C17 authoritative core with opaque handles, bounded allocation, a
  deterministic integer generator, invariant validation, canonical SHA-256
  state, sectioned saves, and authenticated replay;
- a C++20 RAII layer that depends on the C authority rather than duplicating
  rules;
- a no-color, 7-bit ASCII renderer with a C ABI and a terminal client whose
  local, strategic, audit, replay, and hash screens are derived from immutable
  public views; and
- CTest, sanitizer, fuzz, headless-link, deterministic golden, campaign, and
  benchmark targets used by the U01 evidence gate.

Configure and run a strict development build:

```sh
cmake -S . -B build/gcc-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DFD_WARNINGS_AS_ERRORS=ON
cmake --build build/gcc-debug
ctest --test-dir build/gcc-debug --output-on-failure
```

Generate and inspect the fixed U01 world without writing authoritative files:

```sh
build/gcc-debug/frontier_terminal --seed 42 --local \
  --columns 80 --rows 24
build/gcc-debug/frontier_terminal --seed 42 --steps 3 \
  --messages --columns 80 --rows 24
build/gcc-debug/frontier_terminal --seed 42 --interactive
```

`--save FILE`, `--replay FILE`, and `--verify-replay FILE` use canonical binary
containers. Output installation is atomic, and replay verification re-executes
the C rules and checks every recorded boundary hash. Run `--help` for the full
client surface.

The authoritative core and the C++ wrapper can be linked headlessly without the
renderer. The renderer is a separate `frontier_ascii` component; it receives an
immutable snapshot and cannot mutate simulation state.

## Verification status

The update registry and evidence report are the authority for status:

- `docs/updates/UPDATE_REGISTRY.md`
- `docs/evidence/updates/U01.md`
- raw reproducibility artifacts under `evidence/updates/U01/` once the Warden
  candidate is frozen

A passing local test is provisional. Only the independent Warden may move U01
to `VERIFIED`.
