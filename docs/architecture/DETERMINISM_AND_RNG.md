# Determinism and RNG policy

Status: Gate Zero proposal, deterministic policy version 1.

## Determinism tiers

**D1 authoritative:** U01 promises byte-identical generation, steps, canonical
saves, replays, hashes, and ASCII on two's-complement little-endian 64-bit Linux
with approved GCC and Clang configurations. Reference and optimized builds must
match. Additional platforms are unsupported until direct evidence extends D1.

**D2 training:** GPU learner arithmetic may be statistically reproducible under
a versioned backend/precision manifest but is not presumed bit-identical. Policy
actions applied to D1 environments remain authoritative and replayable.

Wall clock, locale, addresses, thread order, host entropy, hash-table order, and
renderer state never enter D1.

## Authoritative numeric policy

- Exact-width unsigned arithmetic is used for RNG and defined modular operations.
- Authoritative game quantities use checked integer/fixed-point operations. A
  failed check returns a structured error before commit; it never wraps or
  silently saturates.
- Money is signed 64-bit minor units. Inventory/capacity are unsigned 32-bit
  units. Ratios use signed 32-bit parts-per-million unless a field-specific
  registry states otherwise.
- Division states rounding explicitly per formula; default is toward zero for
  signed and floor for unsigned. Intermediate widths/checks are specified.
- Authoritative floating point is forbidden in U01.

## Philox4x32-10

Random sampling uses the counter-based Philox4x32-10 bijection with constants:

```text
M0 = 0xD2511F53    M1 = 0xCD9E8D57
W0 = 0x9E3779B9    W1 = 0xBB67AE85
```

Each of ten rounds performs exact unsigned 32-bit high/low multiplication and
XOR/key mixing as defined by the Random123 Philox4x32 specification, then adds
`W0/W1` to the two key words modulo 2^32. Implementation is written from the
public algorithm specification and verified against published vectors; no
proprietary game code or data is used.

The external root seed is 128 bits rendered as exactly 32 lowercase hexadecimal
digits. The U01 convenience API accepting 64 bits expands it by prefixing 64 zero
bits; the canonical manifest stores the full value.

## Semantic scopes

A 256-bit SHA-256 digest is computed over this exact byte sequence:

```text
u32le(26) || "FrontierDirectorate/RNG/v1" ||
u32le(16) || root_seed_le128 ||
u32le(4)  || generator_version_le32 ||
u32le(4)  || ruleset_version_le32 ||
u32le(4)  || rng_domain_id_le32 ||
u32le(8)  || subject_id_le64 ||
u32le(8)  || occurrence_id_le64
```

The domain literal has exactly 26 ASCII bytes and no NUL. Every length and
numeric field is fixed little-endian. For digest bytes `D[0..31]`, with `le32`
loading four bytes, Philox uses:

```text
key[0] = le32(D+0) XOR le32(D+24)
key[1] = le32(D+4) XOR le32(D+28)
ctr[0] = le32(D+8)  XOR low32(logical_sample_index)
ctr[1] = le32(D+12) XOR high32(logical_sample_index)
ctr[2] = le32(D+16) XOR draw_site_id
ctr[3] = le32(D+20) XOR retry_block
```

`draw_site_id` is 32-bit and `retry_block` is 0..255. Generator phase/time is
part of the stable occurrence/subject identity defined by that site's registry
entry. These bytes and mappings are persisted format behavior and have permanent
golden vectors.

Permanent U01 domains include coast, elevation lattice, river source, river
tie-break, biome assignment, settlement placement, road candidate, naming, and
faction initialization. Numeric IDs are registered in source and documentation;
retired IDs are never reused.

Counter words encode semantic phase/time, registered draw-site ID, logical
sample index, and retry block. They never encode worker/thread number, container
iteration index unless that index is itself canonical, process ID, or time.
Parallel jobs partition by stable tile/entity/phase ID.

Adding a sample at one site cannot perturb another site. Debug tracing is
observational and cannot change counters or results.

## Distribution rules and bounds

- A 32-bit uniform value is a Philox output lane selected by semantic index.
- Uniform `[0,n)` uses unbiased rejection sampling, not `% n`. At most 256
  four-lane blocks (1,024 candidates) may be examined; exhaustion returns a
  structured transactional error.
- Bernoulli sampling compares a value with an integer threshold.
- Weighted selection checks every weight and the 64-bit total, then uses unbiased
  bounded sampling.
- Shuffles use descending Fisher-Yates with a named draw site and canonical input
  order.
- Stable ID/coordinate order is preferred for non-random tie-breaking; a random
  tie requires a registered semantic site.
- U01 has no authoritative normal, log, trig, platform distribution library,
  `rand`, `random`, `drand48`, C++ `random_device`, or wall-clock seed.

Generation attempts are semantic occurrence IDs. Rejection reasons and attempt
numbers are diagnostic; the accepted world contains no dependence on timing or
parallel scheduling.

## State, save, observation, and replay

Counter-based samples need no global mutable stream. Persistent sequential event
scopes, if later introduced, store explicit next semantic occurrence counters;
those counters are authoritative, hashed, saved, and replay-audited. Root seeds,
keys, counters, future samples, and debug RNG traces are prohibited actor
observation features.

## Tests

1. Permanent Philox and SHA-256 vectors on every supported compiler/platform.
2. At least one million reference samples match any optimized implementation.
3. U01 outputs match under `-O0/-O2/-O3`, GCC/Clang, repeated processes, and
   supported thread/job counts.
4. Reordered job submission and randomized non-authoritative container insertion
   leave canonical output unchanged.
5. Bounded-distribution exhaustive tests for small domains plus statistical smoke
   tests that detect catastrophic bias; statistics never replace vectors.
6. Static source check rejects prohibited RNG/clock calls in authoritative code.
7. Save/load and replay preserve all persistent semantic occurrence counters.
