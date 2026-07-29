# U01 local candidate verification

This is a concise local verification record for source commit `fe78cc8`. It is
not the complete raw Warden artifact bundle and grants no verification verdict.

## Toolchain

- GCC/G++ 13.3.0
- Clang/Clang++ 16.0.6
- CMake 3.28.3
- Ninja 1.11.1
- Clang-Tidy 16.0.6
- libFuzzer/ASan/UBSan runtime 16.0.6
- x86-64 unprivileged Vast container, non-volume workspace

## Strict builds and tests

Fresh warnings-as-errors configurations passed:

- GCC Debug: 8/8 CTest tests
- Clang Debug: 8/8 CTest tests
- Clang Release: 8/8 non-Warden CTest tests
- GCC ASan+UBSan with leak detection: 8/8 CTest tests
- Clang libFuzzer: all five harnesses built
- Production-source Clang-Tidy: exit 0, no emitted finding

## Candidate campaign

Command:

```sh
ctest --test-dir build/verify-clang-release -L warden --output-on-failure
```

Result: PASS in 32.26 seconds. `test_core` reported 56,110 checks, zero
failures, 10,000 generation seeds, and 1,000 round trips.

## Cross-compiler sample

GCC Debug and Clang Debug generated seed 42, submitted three PASS intervals,
and produced byte-identical save, replay, and 80x24 ASCII files.

- save SHA-256: `fe7dcfa6e1874cfc815a7a0318ad9289645a33a1d4f6d38d681c3c0bc8e409bf`
- replay SHA-256: `f960225027b47073d8edbd8ee8af2e5f0d364d089fa8d6c9856855afa4d504ab`
- ASCII SHA-256: `f2e54608e58b5801ec00fcaa600527a1e41da65b230ad9336809ba08583b5cbe`

## Fuzz smoke

Each ASan+UBSan harness ran from an empty corpus for approximately 60 seconds
with no crash or sanitizer finding:

| Harness | Executions |
|---|---:|
| `fuzz_config` | 692 |
| `fuzz_save` | 256 |
| `fuzz_replay` | 113 |
| `fuzz_action` | 254 |
| `fuzz_api` | 995 |

These runs prove the repaired harnesses build and execute. They do not satisfy
the testing plan's required 10-minute-per-target CI duration.

## Performance summary

Clang 16 Release, 30 repetitions where specified:

| Metric | Result | Frozen target | Outcome |
|---|---:|---:|---|
| Default generation p95 | 3.362 ms | <=10 ms | pass |
| Maximum generation p95 | 95.006 ms | <=100 ms | pass |
| Default/max live state | 29,192 / 787,976 B | <=256 KiB / 4 MiB | pass |
| Default/max save | 28,909 / 787,693 B | <=256 KiB / 4 MiB | pass |
| Scalar PASS mean | 5,579/s | >=10,000/s | fail |
| 64-world/8-thread PASS mean | 22,824/s | >=50,000/s | fail |
| Save mean | 103.391 MiB/s | >=100 MiB/s | pass |
| Load mean | 63.217 MiB/s | >=100 MiB/s | fail |
| Canonical hash p95 | 159.903 us | <=250 us | pass |
| ASCII 120x40 p95 | 56.050 us | <=1 ms | pass |
| Replay record | 228 B | <=2 KiB | pass |
| Ordinary-step allocations | 0 | exactly 0 | pass |

## TSan environment and hosted execution

GCC and Clang TSan configurations compile. The targeted threaded-world binary
cannot start in this container: each runtime exits before application code with
`FATAL: ThreadSanitizer: unexpected memory mapping`. Clang was also tested with
non-PIE compilation and had the same runtime failure.

The repaired POSIX-thread harness executed successfully with GCC 13 TSan on a
GitHub-hosted Ubuntu 24.04 runner. CI run `30457403525` passed all eight jobs at
commit `993832e`, including the threaded-world check, ASan+UBSan, static
analysis, both compiler matrices, and all five fuzz smokes.
