# Testing and verification plan

Status: Gate Zero plan. Tests are evidence only when their requirement coverage
and current artifact hashes are recorded.

## Reference configurations

| ID | Compiler/mode | Required flags or instrumentation |
|---|---|---|
| GCC-DBG | GCC 13+, Debug/reference | `-std=c17` or `-std=c++20`, `-O0 -g3 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror` |
| GCC-REL | GCC 13+, Release | same warnings, `-O3 -DNDEBUG` plus reference differential |
| CLANG-DBG | Clang 16+, Debug/reference | same language/warning intent, `-O0 -g3` |
| CLANG-REL | Clang 16+, Release | same warnings, `-O3 -DNDEBUG` |
| ASAN-UBSAN | GCC/Clang Debug | address + undefined behavior, frame pointers |
| LSAN | supported Linux Debug | leak sanitizer or ASan leak detection, documented |
| TSAN | separate Clang/GCC build | thread sanitizer; independent-world concurrency |

Warnings are not disabled globally. A necessary compiler-specific suppression is
local, documented, and reviewed. GCC and Clang outputs must agree under the D1
promise. AArch64 becomes required only when the determinism support matrix adds
it with direct execution evidence.

## Test layers

1. **Unit:** checked arithmetic, fixed point, IDs, arrays/pools, coordinates,
   graph references, action parsing, masks, update order, Philox, SHA-256,
   canonical encoding, renderer primitives.
2. **Property:** generator bounds/hydrology/terrain/reachability, graph-tile
   agreement, legal starts, conservation and range invariants as systems arrive.
3. **Differential:** reference versus optimized paths after every local tick;
   canonical bytes and hashes identify the first divergence.
4. **Metamorphic:** compiler/optimization, job order, thread count, irrelevant
   content order, allocator address, diagnostic tracing, and render frequency do
   not change authoritative output.
5. **Transactional:** malformed/illegal config/action/file/API sequences publish
   no partial object and preserve live-world bytes, RNG semantics, tick, costs,
   and score.
6. **Integration:** C core + C API + C++ RAII + terminal + save/replay using only
   public boundaries.
7. **Serialization/replay:** canonical round trip, migrations, corruption,
   truncation, size limits, every checkpoint hash, future continuation.
8. **Visibility:** paired worlds differ only in hidden/private/future/RNG/debug
   fields; actor view, actor ASCII, and legal information remain equal.
9. **Scripted agents:** explicit PASS and legal-random first; builder, trader,
   diplomat, and later full baselines use only public views/actions.
10. **Long horizon:** invariant-checked campaigns with retained seeds/replays and
    bounded event/memory growth.
11. **Fuzz:** configuration/content, action prefixes/final/joint batches, save,
    replay, and stateful public-API sequences.
12. **Terminal:** byte goldens, glyph legends, resizes/viewports, input routing,
    no-color equivalence, fog, and render purity.
13. **ABI:** public header from C17 and C++20, sizes/offsets, null/range/buffer
    behavior, lifetime/stale IDs, no exception across callbacks.
14. **Performance correctness:** every benchmark seed ends with the reference
    hash; speed never substitutes for a rule check.

## Fixed seed sets

- Fast CI: 128-bit seeds `0..31` in ascending canonical encoding.
- Nightly: `0..1023`.
- U01 Warden generation campaign: `0..9999`.
- Release: the fixed 10,000 plus 1,000 SHA-256-derived seeds from the release
  commit and 1,000 adversarial/regression seeds.

A discovered failing seed is added permanently; it is never removed just to make
a campaign green. Random campaigns record every exact seed and configuration.

## Update evidence rule

Before implementation, each update maps every feature and cross-cutting impact to
named tests or a reviewed not-applicable rationale. Before `VERIFIED`, the Warden
reruns commands from a clean build and checks raw logs. Required evidence includes
commit/tree hash, compiler/version/flags, platform, configuration/content hashes,
seed set, test counts, failures, skips, durations, sanitizer environment, and
reviewer.

Passing a build, umbrella integration test, or learning curve does not prove
atomic requirements. Skipped required tests fail a release gate.

## Quantitative quality gates

- Every public C API function: success, null, malformed, range, capacity, and
  applicable transactional tests.
- Every authoritative phase registered: at least one order-sensitive test.
- Changed authoritative modules: branch coverage target ≥90%; lower coverage is
  an explicit Warden-reviewed risk, never excluded silently.
- CI fuzz smoke: 10 minutes per applicable target after its introduction.
- Per-update Warden fuzz campaign: at least 1 cumulative hour per changed parser;
  final release target is 24 cumulative hours per target with corpora/crashes.
- Sanitizers: zero findings and zero unexplained suppressions.
- TSan: zero races for concurrent independent environments.
- Static analysis: zero unresolved high-severity findings; all findings retained
  with disposition.
- Long horizon at VS1: 100 fixed seeds × 1,000,000 local ticks unless terminal;
  terminal seeds restart deterministically until total tick budget is met.

## PPO correctness gate

PPO work cannot start until the environment correctness gate passes. U47 must
validate, in order, deterministic bandit, stochastic bandit, tiny tabular MDP,
masked-action MDP, delayed reward, memory task, two-player zero-sum toy,
non-transitive toy, minimal expedition, minimal transport, and two-faction
corridor. Tests include analytic expectations or independent reference oracles,
not “reward increased.” Multiple seeds and confidence intervals report all runs.

## Baseline tooling gap

At Gate Zero baseline, `clang-tidy`, `cppcheck`, coverage aggregation, and
benchmark tooling were absent and no test target existed. Before U01 Warden
review, the project must pin/install an analyzer or record an approved equivalent
command, add CTest targets and presets, and archive raw results. This gap is not a
passing or not-applicable static-analysis result.

