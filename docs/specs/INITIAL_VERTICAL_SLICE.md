# Frozen initial vertical-slice specification

Specification ID: `FD-VS1`  
Version: 1.0-gate-zero  
Status: frozen; Gate Zero independently approved 2026-07-29

## Scope and milestone

VS1 is the first playable corridor campaign accumulated through U28. It is not
all assigned to U01. Each update remains independently implementable and must
not pre-implement later mechanics merely to stage a demo.

U01 is fully specified in this document and may begin after Gate Zero passes.
U02–U28 require their own specifications before implementation.

## Campaign contract

One local sovereign polity owns a coastal and inland settlement in one coastal
region. Two foreign expedition factions compete, with equal rule access but
different procedurally generated starting aptitudes and imperfect knowledge, to
establish a reliable and politically sustainable transport corridor. The local
polity pursues its own security, revenue, provisioning, and autonomy objectives;
it is not a neutral map feature.

The standard campaign lasts 720 in-world days. It can terminate earlier if no
foreign faction retains a viable expedition, if the local polity revokes all
access for cause with no legal recovery path, or if a faction meets the
sustained-corridor condition for 90 consecutive days.

A sustained corridor requires, over the trailing 90 days:

- at least 70% of accepted contracted cargo volume delivered on time;
- at least 50% of original expedition personnel alive and no untreated critical
  case when treatment capacity is available;
- non-negative operating cash flow, excluding initial grant funding;
- at least one coastal-to-inland legal route with 75% scheduled-trip completion;
- relation with the local polity at least 0 on `[-100,100]`;
- valid settlement/transit access and zero unresolved material treaty breaches;
- mandate progress at least 70/100.

If both factions satisfy it on the same decision boundary, rank by the tuple:
mandate progress, treaty-compliant delivered volume, expedition survival basis
points, operating cash flow, local relation, then stable faction ID. The stable
ID is only a deterministic last tie-break, never a gameplay bonus.

## World and bounds

| Property | VS1 default | Valid U01 range |
|---|---:|---:|
| Width × height | 48 × 24 | 32–256 × 16–128 |
| Tile count | 1,152 | maximum 32,768 |
| Region nodes | 1 | exactly 1 in U01 |
| Settlements | 2 | exactly 2 in U01 |
| Local sovereign polities | 1 | exactly 1 in U01 |
| Foreign expedition factions | 2 | exactly 2 in U01 |
| River | 1 primary | exactly 1 connected, acyclic outlet path in U01 |
| Road candidate | 1 | exactly 1 settlement-to-settlement potential corridor |

Required base terrain is water, grassland, forest, hills, wetlands, and
settlement footprint. River and road candidate are overlays and therefore do
not erase underlying elevation/traversability. Every tile stores bounded integer
elevation and rainfall values for later systems, though U01 attaches no weather
behavior.

### U01 constructive generator

The generator accepts generator version, configuration, 64-bit seed, and a
sorted list of content-pack SHA-256 hashes. It must finish within configured
attempt budgets or return a structured error without a partial world.

Generation phases, each using its own named RNG stream, are:

1. Validate dimensions, density ranges, attempt budgets, and content hashes.
2. Construct a west- or east-facing irregular coast with one connected landmass
   covering 55–80% of tiles; select coast side deterministically.
3. Generate bounded elevation from integer multi-octave value fields; apply a
   deterministic coast-distance bias. Ties use `(y,x)` order.
4. Select a hill source at least eight orthogonal steps from the coast and trace
   a strictly decreasing-or-carved river to one sea outlet. Carving may lower an
   uncommitted elevation but can never create a loop or off-map flow.
5. Assign grassland, forest, hills, and wetlands using integer thresholds;
   wetlands must touch the river or coast. All six required render types must be
   present or the bounded attempt is rejected.
6. Place one coastal settlement on land adjacent to both sea and river outlet,
   and one inland settlement on the same land component at Manhattan distance
   at least 12 from it and at least four from sea.
7. Create one road-candidate overlay using stable-cost Dijkstra between
   settlement entry tiles. It cannot traverse sea; it may cross the river at at
   most one designated future bridge/ford candidate. It grants no movement or
   ownership behavior before U14.
8. Create the region, local polity, two foreign factions, ownership/membership,
   and typed physical graph references. Settlements remain locally owned.
9. Validate every invariant, derive canonical state hash, and emit a generation
   manifest. Failed attempts do not leak partially initialized state.

The maximum default attempt budget is 64 complete candidates. Every rejected
candidate records a stable reason code and attempt index in diagnostics. A
constructive fallback using the final attempt must satisfy the same invariants;
if configuration makes that impossible, return `FD_ERR_GENERATION_EXHAUSTED`.

Names use project-owned syllable/content tables and stable IDs. They may not use
names or naming tables from an inspirational game or real people by default.

## Linked representations

The U01 tile world is authoritative for terrain, elevation, river/road overlays,
settlement footprints, and placement. The U01 strategic view contains one
region node and typed references for both settlements, the river outlet, the
candidate corridor, political owner, and foreign presences. Every physical
reference names existing tile IDs; the political owner names the local polity.
Validation fails on any mismatch.

## U01 state, behavior, and non-scope

U01 adds generated geography, stable entity identities, read-only queries,
canonical hash, save/load, generation replay, and ASCII local/strategic views.
It supports a deterministic reference step with only `PASS`, so replay plumbing
is real without inventing later rules.

U01 explicitly does **not** implement personnel attributes, inventories, fog,
movement modifiers, weather, polity decision logic, diplomacy, trade, road
construction or bonuses, rewards, PPO tensors, or victory scoring. Their future
state is neither stubbed nor credited. Identity queries needed to show the two
foreign factions and local polity are not those later mechanics.

## VS1 accumulated content

By U28 the slice includes all charter-listed infrastructure (coastal/river docks,
trading post, dirt road, warehouse, depot, hospital, camp), transport (porter,
pack caravan, wagon, riverboat), goods (food, medicine, tools, construction
materials, trade goods, mail, passengers), and strategic systems (treasury,
supply/capacity/time, weather, disease, morale, relations/access,
construction/delivery/survival, and mandate). U03 also introduces tents and
spare parts as required by its update; passengers are entities, not fungible
inventory. The update registry, not this list, governs credit for each feature.

At the accumulated VS1 start, the local polity owns an operating coastal dock,
the inland trading post, and one coastal warehouse. Each expedition owns one
coastal camp and its starting portable inventory. Dirt road segments, a river
dock, supply depot, and field hospital are available construction outcomes, not
prebuilt demonstrations. Exact inventories/costs are frozen in their owning
update specifications before implementation.

## Time contract

Authoritative time is an integer count of one-hour local ticks. Operational
decisions occur every 24 ticks, strategic turns every 168 ticks, economic
periods every 720 ticks, seasons every 2,160 ticks, and years every 8,640 ticks
(a 360-day abstract calendar). Calendar labels are presentation derived from the
tick count. U01 reference steps advance one operational interval and run all 23
stages even when a stage has no entities.

The 23-stage order in charter section 11 is normative. Stage and entity order
are logged in debug reference mode and cannot depend on threads or unordered
containers.

## U01 public/agent representation

All U01 facts are queryable through stable C API views. `PASS` is the only legal
category and its one-bit exact mask is exposed. There is no reward in U01; the
step result reports reward count zero, avoiding a fabricated learning signal.
Observation metadata identifies map/entity facts as omniscient debug/reference
views only; ordinary partial actor observations begin with U05. This is an
explicit not-applicable result for PPO, not a placeholder tensor.

## U01 ASCII contract

Local and strategic screens follow `docs/architecture/ASCII_INTERFACE.md`.
Required U01 glyphs and legend entries are water `~`, grass `.`, forest `f`,
hills `^`, wetlands `v`, river `r`, road candidate `:`, coastal settlement `c`,
inland settlement `n`, and selected/inspected tile `@`. Faction and polity
identities appear in an inspector panel, not as units that do not yet exist.

ASCII output for a state and viewport is byte-identical, ends every row with
`\n`, contains no control sequences in ASCII mode, and cannot mutate state.

## U01 save/replay contract

Saves contain all generated authoritative fields, generator/config/content
identity, the RNG semantic-scope registry version and any persistent occurrence
counters, IDs, time, and canonical hash. U01's counter-based generator has no
mutable global or subsystem stream state.
Replays contain generation identity, accepted joint `PASS` decisions, structured
diagnostics, and a state hash at tick zero and every operational boundary. Load,
round trip, truncation, corruption, unknown mandatory section, and deterministic
replay behavior follow `SAVE_AND_REPLAY.md`.

## U01 acceptance criteria

U01 may become `IMPLEMENTED_UNVERIFIED` only when all of these have raw evidence:

1. GCC and Clang compile C sources strictly as C17 and the C++ wrapper/client as
   C++20 with warnings-as-errors in the documented reference configurations.
2. Same input identity yields byte-identical canonical saves, ASCII, and hashes
   across repeated runs and the promised GCC/Clang Linux builds.
3. At least 10,000 configured seeds pass bounds, land/coast, terrain coverage,
   hydrology, settlement, candidate-corridor, ownership, graph, and objective
   reachability properties; every failure is reported by seed and invariant.
4. Invalid configurations and exhausted generation fail transactionally with a
   structured error and no live world handle.
5. Save/load and replay round trips cover at least 1,000 seeds; corrupted,
   truncated, oversized, wrong-version, and unknown-mandatory-section inputs are
   rejected without leaks or partial output.
6. Rendering tests prove legend completeness, viewport bounds, ASCII-only bytes,
   local/strategic distinction, snapshot goldens, and unchanged before/after
   state hashes. Headless tests link and step without invoking renderer symbols.
7. C API tests cover nulls, sizes, ranges, ownership, stale IDs, buffer-too-small
   negotiation, error diagnostics, and C++ RAII lifetime behavior.
8. ASan+UBSan and LSan-supported runs pass; TSan passes concurrent independent
   worlds. Static analysis has no unresolved high-severity finding. Fuzz targets
   for config, save, replay, action, and public API survive the documented Gate
   Zero smoke duration before Warden review.
9. Reference mode performs zero heap allocations per ordinary step, confirmed by
   an allocation counter. Maximum configured state is bounded and measured.
10. Benchmarks report default/max generation time, state/save size, reference
    step rate, render time, serialization rate, peak memory, and raw repetitions
    under the performance-plan provenance rules. Targets on the baseline class
    of hardware are default-world p95 generation ≤10 ms, max-world p95 ≤100 ms,
    default canonical save ≤256 KiB, and max save ≤4 MiB; failure is reported,
    never hidden.
11. Independent Warden review reruns the evidence, confirms no copied
    inspirational-game assets, code, data, layout, glyph assignment, or
    implementation detail, and is the only authority that changes U01 to
    `VERIFIED`.

## Change control

Any change to generator phases, bounds, terrain meanings, time scale, glyphs,
hash, save layout, or acceptance thresholds increments this specification and
records migration/test/evidence impacts. No implementation convenience silently
changes this frozen contract.
