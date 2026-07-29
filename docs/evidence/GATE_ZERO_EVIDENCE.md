# Gate Zero evidence report

Evidence ID: `FD-G0-EV-001`  
Charter: `GOV-001` SHA-256
`5235d46f948d8d233cecf61fdfb18622c02977935168578fbab46c6d10f21fb6`  
Status: **PASS — INDEPENDENTLY APPROVED**

## Scope

Gate Zero authorizes specification and baseline work only. It does not implement
or verify U01, and it gives no completion credit to U02–U50. Production code must
not begin until the approval record below says `PASS` and is signed by a Warden
who did not author the reviewed decisions.

## Baseline results

| Gate | Result | Evidence |
|---|---|---|
| Governing documents read and hashed | COMPLETE | `docs/governance/GOVERNING_DOCUMENTS.md`, raw SHA256SUMS |
| Initial repository/Git status | COMPLETE: no repository existed | `docs/baseline/REPOSITORY_BASELINE.md` |
| Initial build | ABSENT / NOT RUN | baseline report/results |
| Initial tests | ABSENT / NOT RUN | baseline report/results |
| Initial sanitizers | configuration absent / NOT RUN | baseline report/results |
| Initial static analysis | configuration absent / NOT RUN | baseline report/results |
| Initial benchmarks | ABSENT / UNMEASURED | baseline report/results |
| Initial implementation | 0/50 updates implemented; 0/50 verified | baseline report |
| Post-baseline provenance foundation | local Git initialized on `main`; no remote | current Git state |

Missing implementation is not an external blocker and none of the absent checks
is represented as passing.

## Required deliverables

| # | Deliverable | Authoritative path | Director self-audit |
|---:|---|---|---|
| 1 | Governing inventory/hashes | `docs/governance/GOVERNING_DOCUMENTS.md` | PRESENT |
| 2 | Git/repository status | `docs/baseline/REPOSITORY_BASELINE.md` | PRESENT |
| 3 | Build inventory | baseline report | PRESENT |
| 4 | Dependency inventory | baseline report | PRESENT |
| 5 | Source/current-implementation inventory | baseline report | PRESENT |
| 6 | Test inventory | baseline report | PRESENT |
| 7 | Sanitizer/static-analysis inventory | baseline report | PRESENT |
| 8 | Benchmark inventory | baseline report | PRESENT |
| 9 | Documentation inventory | baseline report | PRESENT |
| 10 | Requirement traceability | `docs/requirements/REQUIREMENT_TRACEABILITY.md` plus atomic catalogs | PRESENT |
| 11 | Update registry | `docs/updates/UPDATE_REGISTRY.md` | PRESENT; U01 SPECIFIED only |
| 12 | All-50 dependency graph | `docs/updates/dependencies.json` and explanation | PRESENT; JSON validates 50 nodes |
| 13 | Specification conflicts | `docs/governance/SPECIFICATION_CONFLICTS.md` | PRESENT; 21 resolved |
| 14 | Architecture decision register | `docs/architecture/DECISION_REGISTER.md` | PRESENT; 24 accepted for G0 |
| 15 | Risk register | `docs/governance/RISK_REGISTER.md` | PRESENT; risks remain actively owned |
| 16 | Frozen initial vertical slice / U01 spec | `docs/specs/INITIAL_VERTICAL_SLICE.md` | PRESENT |
| 17 | Initial C API proposal | `docs/architecture/C_API_PROPOSAL.md` | PRESENT |
| 18 | Initial save/replay proposal | `docs/architecture/SAVE_AND_REPLAY.md` | PRESENT |
| 19 | Initial ASCII specification | `docs/architecture/ASCII_INTERFACE.md` | PRESENT |
| 20 | Deterministic RNG/time/numeric policies | `docs/architecture/DETERMINISM_AND_RNG.md`, `NUMERIC_AND_TIME_POLICY.md` | PRESENT |
| 21 | Testing and performance plans | `docs/quality/TESTING_PLAN.md`, `PERFORMANCE_PLAN.md` | PRESENT |

`PRESENT` is an author inventory, not verification. The Warden verdict below is
the gate authority.

## Consistency checks to rerun

```sh
jq -e '.schema and (.updates|length==50) and ([.updates[].id]|unique|length==50)' docs/updates/dependencies.json
test "$(rg -c '^\\| [0-9][0-9] \\| FD-U' docs/updates/UPDATE_REGISTRY.md)" -eq 50
test "$(rg -c '^\\| FD-UPD-' docs/requirements/ATOMIC_REQUIREMENT_CATALOG.md)" -eq 399
rg -n 'VERIFIED' docs/updates/UPDATE_REGISTRY.md
sha256sum /root/.codex/attachments/48b451ff-95d7-400b-ba0d-203f03b8a55c/pasted-text-1.txt
git status --short --branch
```

Expected: JSON succeeds; registry has 50 rows; 399 bullet feature items exist
across updates (U41's prose requirement is separately cataloged); the only
registry uses of `VERIFIED` are policy prose/allowed state, never an update row;
the charter hash matches; Git shows only the reviewed Gate Zero package before
its first commit.

## Independent inputs already received

- Baseline worker `/root/repo_baseline` performed read-only inventory and caught
  the explicit-Clang and sanitizer-runtime distinctions, both corrected here.
- Requirements worker `/root/requirements_analysis` independently derived the
  semantic dependency DAG, conflict set, atomic-ID model, risks, and VS1 gates.
- Interface worker `/root/interface_specs` independently proposed the API,
  save/replay, RNG, ASCII, test, and performance boundaries. The director chose
  SHA-256 rather than BLAKE3 and documented that decision; counter-based Philox
  and the stronger semantic-scope policy were adopted.

These are worker contributions, not Warden approval.

## Known follow-up requirements, not Gate Zero concealments

- The baseline lacked project build/test/analyzer targets. U01 implementation
  must create and pass them before it can be reviewed.
- There is no Git remote and `/workspace` is non-persistent. Significant
  production work must be synchronized off-box when a destination is available;
  no destination is fabricated by this report.
- U02–U50 remain honestly `UNSPECIFIED`; each needs an update specification
  before its own implementation.
- Later-phase choices (PPO backend, web stack, optional Python bindings, final
  content schema) remain deferred to their recorded decision points, as allowed
  by the charter.

## Warden approval record

| Field | Value |
|---|---|
| Warden identity | `/root/gate_zero_warden_2` |
| Review time | 2026-07-29 UTC |
| Reviewed tree digest | `ec0d09fcd69411690679df5ee0ff0ee21e5be4db` |
| Commands rerun | governing hashes; exact catalog/source comparison; trace/range/conflict-ID validators; registry/DAG uniqueness, edges, cycles, reachability; ABI constant scan; transcript pairing; Git tree/source inventory |
| Findings | No blocking findings; no nonblocking findings |
| Verdict | **PASS** |
| Authorization | Gate Zero authorizes beginning U01 only; it does not implement or verify U01 |

## Review history

| Review | Warden | Reviewed tree | Verdict | Blocking findings |
|---|---|---|---|---|
| G0-R1 | `/root/gate_zero_warden` | `ce3ac9acc594ec42d1d8292d4d27c756952fd3b4` | **FAIL** | Eight update-range mismatches; 12 traceability predecessor contradictions; incomplete prose catalog claim; conflict register missing its required impact fields; fixed-width ABI contradicted by public enums |
| G0-R2 | `/root/gate_zero_warden_2` | `ec0d09fcd69411690679df5ee0ff0ee21e5be4db` | **PASS** | None; 1,044 list and 165 prose rows exact, 50 trace rows/399 features exact, 21 impact rows/297 references valid, 50-node/217-edge DAG valid, fixed-width ABI explicit, no production code |

The director corrected R1 findings without treating them as approval. R2 reviewed
the new exact tree, reran the catalogs, dependency, conflict-impact, ABI,
provenance, and no-production-code checks, and independently authorized U01 to
begin. Only this metadata record was filled after the reviewed digest; it does
not modify a specification decision.
