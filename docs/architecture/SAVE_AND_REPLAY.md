# Initial save and replay proposal

Status: Gate Zero proposal, format version 1 design.

## Canonical encoding

- Files use little-endian fixed-width integers and length-delimited sections.
- Native structs, padding, addresses, container iteration order, locale values,
  and authoritative floating-point objects are never serialized.
- Header and section arithmetic is checked before allocation or pointer access.
- Maps serialize row-major by `(y,x)`. Entities, graph nodes/edges, content IDs,
  and pending events serialize by stable numeric key.
- Strings are bounded, valid UTF-8, normalized to NFC at content ingestion, and
  then preserved byte-for-byte. ASCII fallback transliteration is not state.
- Each section header is `{tag:u16, flags:u16, reserved:u32, length:u64}`.
  Reserved bits must be zero. Unknown `CRITICAL` sections reject the file;
  unknown optional sections are skipped only after bounds validation.
- Each section has a SHA-256 payload digest. The footer carries a domain-separated
  SHA-256 digest over every preceding canonical byte. SHA-256 is pinned by NIST
  vectors and is also the canonical state-hash primitive.

## Save container

Magic is the eight bytes `FDVS\0\0\0\1`. Required sections in version 1 are:

1. format manifest and compatible reader range;
2. API, ruleset, world-generator, save, and replay versions;
3. canonical configuration plus its SHA-256;
4. sorted content-pack stable IDs and SHA-256 hashes;
5. root seed and RNG semantic-scope registry version;
6. time-scale definition and current integer clocks;
7. complete authoritative tile and region state;
8. complete entity tables, generations, and capacity limits;
9. RNG counters for persistent sequential event scopes, if any;
10. pending commitments/events and terminal/objective state;
11. canonical state hash;
12. footer digest.

The authoritative snapshot includes every hidden fact capable of changing a
future transition. It excludes viewport, terminal capability, renderer caches,
allocation address/capacity slack, profiler counters, wall-clock timestamps,
debug logs, and caches deterministically derivable from included fields.

Load validates the complete container into a temporary candidate, rebuilds
derived caches, checks all tile/graph/state invariants and the canonical hash,
then publishes the world handle atomically. Failure publishes no handle and does
not alter another world.

## Replay container

Magic is `FDRP\0\0\0\1`. A replay is an audit record, never an alternative rule
engine. Required records are:

- manifest and canonical tick-zero checkpoint;
- submitted joint decisions in canonical seat/action form, including invalid
  attempts in a non-authoritative audit channel;
- validation result and structured diagnostic for rejected attempts;
- for accepted intervals: pre-state hash, joint decision, emitted-event digest,
  raw per-faction objective/reward components where they exist, and post-state
  hash;
- RNG audit counts by semantic domain, never exposed to actor observations;
- a full checkpoint every 1,024 operational decisions and at terminal state;
- a footer digest.

The tick-zero checkpoint is mandatory even though provenance can regenerate it.
This permits future inspection when an old generator binary is unavailable and
enables regeneration-versus-checkpoint comparison.

At simulation phase 22, the core stages the transition record. Phase 23 computes
the canonical post-state hash. The core then finalizes the non-authoritative
record with that hash without applying more simulation state. Thus replay never
alters the specified 23-stage rule order.

Invalid action attempts may append to an external diagnostic/replay sink, but
that sink is excluded from authoritative state, saves, rewards, RNG counters,
and canonical hashes. A rejected action cannot advance time or mutate a live
world.

## Modes

- **Verify:** load/regenerate tick zero, apply decisions through the C core, and
  require every intermediate hash and event digest.
- **Inspect/seek:** load the nearest earlier checkpoint and resimulate through
  the core. An index can accelerate lookup but is non-authoritative.
- **Event-only display:** render recorded summaries; it cannot claim deterministic
  reproduction.

C++ owns atomic file replacement, compression envelopes, indexes, and paths. It
does not generate authoritative records or resolve transitions.

## Compatibility and migrations

- A major format mismatch is rejected with `FD_ERR_VERSION`.
- A minor format may add optional sections/fields only.
- Ruleset changes increment the ruleset version even when ABI/format is stable.
- Save migrations are explicit pure `N -> N+1` transformations with golden old
  fixtures, expected new bytes/hashes, and future-continuation tests.
- Replays are not silently migrated across changed game rules. Approved tooling
  may extract a checkpoint into a new scenario only with a provenance record; it
  is not the original replay.
- Truncation, overflow, duplicate singleton sections, noncanonical order,
  invalid UTF-8, impossible counts, digest mismatch, and trailing unframed bytes
  are hard structured errors.

Every update evidence report states `UNCHANGED`, `EXTENDED_COMPATIBLY`, or
`MIGRATED` for save and replay, with direct tests. No visible or strategic update
may mark these fields not applicable merely because its happy path runs.

## Acceptance tests

1. Save-load preserves state hash and at least 10,000 subsequent reference ticks.
2. Save-load-save is byte-identical for canonical current-version files.
3. Replay verification checks every interval hash, not only final state.
4. Corruption, truncation, oversized lengths/counts, duplicate mandatory data,
   unknown critical sections, and version mismatches fail without partial output.
5. Loader/replay fuzz targets remain within configured allocation/file bounds and
   are sanitizer-clean for the required campaign.
6. GCC/Clang reference builds produce and consume identical U01 fixtures. The
   portability promise expands only with recorded platform evidence.
7. Golden fixtures from every verified format version remain in the suite and
   are never rewritten in place.

