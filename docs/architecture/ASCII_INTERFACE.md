# Initial ASCII interface specification

Status: Gate Zero proposal. ASCII is the normative terminal representation;
Unicode and color are optional, information-equivalent themes.

## Authority boundary

The C++ terminal client renders immutable C snapshots and submits commands
through the same C action API used by agents. It does not calculate legality,
visibility, prices, movement, outcomes, or rewards. Rendering and navigation do
not mutate state. Headless executables do not link or initialize terminal code.

UI-only state (viewport, cursor, pane focus, scroll, color, terminal size) is not
saved or hashed. Replay bookmarks, if added, are separate user metadata.

## Terminal contract

- Minimum supported size is 80×24; preferred reference is 120×40. Below minimum,
  the client renders a deterministic size notice and does not discard state.
- ASCII mode emits printable bytes `0x20..0x7e`, `\n`, and no control sequences
  when color/input control is disabled.
- The `C` locale governs display formatting. Canonical calendar/numeric strings
  are derived from integer state, never host time, timezone, or locale.
- Every frame contains a title/mode, authoritative clock and pause/terminal
  status, viewport/table, inspector, legend/columns, message line, and key hint.
- Local and strategic maps have different titles, scale labels, borders,
  coordinate presentation, and legends.
- Color is redundant with glyph/text. Removing ANSI sequences from a color frame
  retains every fact and legal interaction.

## U01 glyph registry

| Glyph | Meaning | Layer |
|---|---|---|
| `~` | sea/water | base |
| `.` | grassland | base |
| `f` | forest | base |
| `^` | hills | base |
| `v` | wetland | base |
| `r` | river overlay | physical overlay |
| `:` | possible road corridor, not built | planning overlay |
| `c` | coastal-settlement footprint | structure |
| `n` | inland-settlement footprint | structure |
| `@` | selected/inspected tile | selection |
| `?` | unknown tile, from U05 | visibility |
| `!` | visible incident, when implemented | incident |
| `+` | multiple visible occupants | occupancy |

Overlay priority is selection, visible incident, multiple occupancy, visible
unit/vehicle, building, built infrastructure, planning overlay, river, base
terrain. Selecting a tile replaces its map glyph with `@`; the inspector states
the underlying glyphs and authoritative visible values. Unknown contents never
affect an actor-facing glyph.

Every map legend lists every glyph currently present in the viewport and may
list stable controls. A glyph cannot carry two meanings on the same screen.

## U01 local screen

The 80×24 layout reserves two header lines, a bordered 48×16 or smaller viewport,
a context-sensitive legend, a selected-tile inspector, a message line, and key
hint. Larger terminals expand the viewport before the inspector. It shows seed
manifest short hash, date/tick, region, map coordinates, terrain/elevation,
physical overlays, settlement/polity ownership, and whether the view is the
explicit omniscient reference scope.

## U01 strategic screen

The strategic screen renders the one region as a labeled node, its two settlement
members, local political ownership, foreign presences, river outlet, and
road-candidate physical link. It does not fabricate influence, treaties, markets,
or supply. Its scale and graph legend make it unmistakable from the tile map.

## Required screen allocation

A screen is delivered only when its earliest authoritative data exists; until
then it is not counted or implemented as a fake zero-filled panel.

| Screen | Earliest owning update |
|---|---:|
| Local map, strategic region map, message log, replay inspector, debug hash | U01 |
| Character roster, expedition status, faction identity overview | U02 |
| Markets | U11 |
| Transport network | U12 |
| Diplomacy | U10 |
| Construction | U14 (camp actions may use expedition screen earlier) |
| Supply network | U15 |
| Production ledger | U19, extended U20/U21 |
| Intelligence | U32 |
| Military | U34 |
| Events | first typed event producer, fully extended U44 |
| PPO action inspector | U47 |

Each visible update amends the registry and supplies snapshot tests; a menu label
or “coming soon” panel is not implementation credit.

## Controls

`?` help, `q` leave screen/client, arrows or `h/j/k/l` move, `Tab` cycles panes,
`Enter` inspects/confirms, `Esc` cancels, `m` switches local/strategic, `[`/`]`
changes supported scale, `PgUp/PgDn` scrolls, and `/` filters. A simulation
command always passes through legal-prefix queries and final C validation.

## Accessibility and visibility

ASCII-only, no-color operation must expose the complete interaction surface.
Optional Unicode glyphs have a one-to-one ASCII semantic mapping. Actor frames
use only faction-filtered snapshots. Administrative views are titled
`ADMIN/OMNISCIENT` and unavailable to an ordinary policy/client handle.

## Acceptance evidence

- golden frames at 80×24 and 120×40, color disabled;
- viewport/resize/bounds and deterministic output tests;
- every present map glyph appears in that frame's legend;
- rendering before/after leaves save bytes and state hash unchanged;
- ASCII and Unicode themes show identical fields/actions;
- paired hidden-state tests prove actor frames are unchanged;
- headless linkage/call tracing proves no renderer invocation;
- provenance/design review confirms no copied layout or glyph assignment.

