# Numeric and simulation-time policy

Status: Gate Zero policy for U01; later fields extend the numeric registry before
implementation.

## Numeric registry

| Class | Representation | Unit/range | Overflow response |
|---|---|---|---|
| Tick/time | `uint64_t` | hours, `0..UINT64_MAX-1` | transactional `FD_ERR_CAPACITY` |
| Money | `int64_t` | minor currency units; configured safe domain | transactional `FD_ERR_CAPACITY` |
| Inventory/capacity | `uint32_t` | whole canonical good units | transactional `FD_ERR_CAPACITY` |
| Population aggregate | `uint32_t` | persons or documented cohorts | transactional `FD_ERR_CAPACITY` |
| Health/morale/mandate | `int32_t` | basis points `0..10000` | reject invalid/range result |
| Relations | `int32_t` | basis points `-10000..10000` | reject invalid/range result |
| Ratios/rates | `int32_t` | parts per million unless field says otherwise | checked widened intermediate |
| Elevation | `int32_t` | abstract centimeters above datum | checked/clamped only during documented generator normalization |
| Rainfall | `uint32_t` | abstract millimeters/year | checked generator bound |
| Coordinates/counts | `uint32_t` | tiles/items within configuration caps | prevalidated multiplication/addition |
| Entity IDs | `uint64_t` | generation:slot, zero invalid | stale/invalid structured error |

All authoritative arithmetic uses named checked helpers. Saturation is permitted
only when a mechanic explicitly defines saturation and tests the boundary; it is
never an overflow fallback. Signed division rounds toward zero, unsigned division
floors, and any other rounding rule appears beside the formula.

## Clocks

Local tick = 1 abstract hour. Operational interval = 24 ticks. Strategic turn =
168 ticks. Economic period = 720 ticks. Season = 2,160 ticks. Year = 8,640 ticks
(360 abstract days). All are derived from `uint64_t tick`; redundant calendar
fields are presentation-only.

## Normative transition order

An accepted operational joint decision executes 24 local transitions. At each
applicable boundary, the following stable sequence runs:

1. validate previous state;
2. commit already validated joint decisions at the first local tick of interval;
3. resolve action conflicts;
4. advance construction;
5. update weather;
6. update movement using weather just computed for this tick;
7. process arrivals;
8. transfer cargo/passengers;
9. process production at configured production boundaries;
10. process consumption;
11. update supply;
12. update health/disease;
13. update morale;
14. process markets at economic boundaries;
15. process diplomacy at decision boundaries;
16. process political influence;
17. process military encounters;
18. process finances at economic boundaries plus committed transaction entries;
19. update settlements at economic boundaries;
20. detect failures and terminal state;
21. emit typed events;
22. stage replay transition data;
23. compute the canonical state hash.

Within a stage, regions, factions, entities, tiles, inventory keys, and events
iterate by ascending stable numeric key unless the subsystem specification names
another canonical order. Proposed mutations are collected into bounded staging
buffers, conflict-resolved, and committed at the stage's stated boundary. No
thread timing or unordered-container traversal determines an outcome.

After phase 23, non-authoritative replay instrumentation may append the computed
hash to the phase-22 staged record. It cannot touch simulation state.

U01 runs every phase even though most have no applicable entities. Its only
accepted action is explicit `PASS`; this exercises ordering, replay, and hashes
without fake later mechanics.

