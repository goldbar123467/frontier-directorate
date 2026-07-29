# Repository baseline

Baseline time: 2026-07-29 UTC  
Workspace: `/workspace`

## Repository and Git status

Before Gate Zero artifacts were created, `/workspace` contained only:

- `.hf_home/`
- `.venv-backups/46154084/` with environment backup metadata
- `AGENTS.md` and `CLAUDE.md`, both symlinks to `/etc/vast-agents-guide.md`

There was no `.git` directory. `git status` and `git log` both returned
`fatal: not a git repository`. There was no source tree, build definition,
release artifact, package manifest, or project documentation.

After recording that baseline, Gate Zero initialized a local Git repository on
branch `main` so subsequent evidence can name commits. This post-baseline action
does not rewrite the initial finding. No remote was available or invented.

## Build inventory and status

| Item | Baseline evidence | Status |
|---|---|---|
| Build system | no `CMakeLists.txt`, Meson, Make, Bazel, or other project build files | ABSENT |
| C build | no C source and no build definition | ABSENT / NOT RUN |
| C++ build | no C++ source and no build definition | ABSENT / NOT RUN |
| Strict-warning build | no build target | ABSENT / NOT RUN |
| Clean build | no build target | ABSENT / NOT RUN |

An absent build is a baseline fact, not a passing build.

## Dependency inventory

No project dependency manifest or vendored dependency existed. Available host
tools and libraries are environmental capabilities, not declared project
dependencies.

| Capability | Version/status |
|---|---|
| GCC C/C++ | 13.3.0 |
| Clang | unsuffixed launcher absent; `clang-16`/`clang++-16` 16.0.6 present |
| CMake | 3.28.3 |
| Ninja | 1.11.1 |
| GNU Make | 4.3 |
| Git | 2.43.0 |
| pkg-config | 1.8.1 |
| Python | 3.12.13 |
| Node / npm | 24.16.0 / 11.13.0 (after sourcing nvm) |
| OpenSSL/libcrypto | available through pkg-config |
| SQLite3, zlib, libcurl | available through pkg-config |

Initial production dependencies remain deliberately undecided until their use
is justified and pinned. ADR-0001 selects a standard-library-only C core for
Update 01.

## Current source inventory and implementation assessment

There were zero project C, C++, header, Python, JavaScript, TypeScript, or web
source files. Consequently none of the 50 updates was implemented, integrated,
tested, benchmarked, rendered, serialized, replayable, agent-accessible, or
independently verified. This assessment is explicit; missing implementation is
not an external blocker.

## Test inventory and status

No test source, test manifest, fixtures, golden files, fuzz corpus, or test
runner configuration existed. CTest 3.28.3 and pytest 9.1.1 are available in
the environment, but no project test was discovered or run. Unit, property,
differential, integration, replay, serialization, long-horizon, scripted-agent,
PPO, ASCII, protocol, and end-to-end status are all **NOT RUN / ABSENT**.

## Sanitizer and static-analysis inventory

No sanitizer build configuration or result existed. GCC 13.3.0 can provide
ASan, UBSan, LSan, and TSan configurations once build targets exist. At
baseline: ASan, UBSan, LSan, and TSan are all **NOT RUN**.

The unsuffixed `clang` launcher was absent, but explicit `clang-16` and
`clang++-16` binaries were present. `clang-tidy`, `cppcheck`,
Include-What-You-Use, and Valgrind were absent. `gcov` 13.3.0 was present;
`lcov` and `llvm-cov` were absent. GCC could resolve the ASan, UBSan, LSan, and
TSan runtime libraries, but no project sanitizer configuration existed. Static
analysis and sanitizer execution therefore had no result and must not be
described as passing.

## Benchmark inventory and status

No benchmark source, runner, baseline commit, raw output, or statistical report
existed. `hyperfine` was absent. Every metric required by charter section 23 is
**UNMEASURED**. The instance manifest available during baseline reports an RTX
5080, x86-64 Linux, and a non-persistent workspace, but GPU capability is not a
claim that any game code is GPU accelerated.

The host reported Linux 6.8.0-124-generic on x86-64, Ubuntu 24.04.4, 64-bit
userspace, and `nproc` 128. Live portal metrics reported a 61-CPU allocation,
while the shell affinity view exposed 128 logical processors; benchmark
artifacts must capture both affinity and portal allocation instead of assuming
they are equal. No compiler flags existed at baseline.

## Documentation inventory

No project documentation existed. The only visible Markdown paths were the two
instance-guide symlinks. Gate Zero creates the first project documentation set.

## Environment constraints relevant to the project

- `/workspace` is not a mounted persistent volume; recycle or destroy loses the
  repository unless it is synchronized off-box.
- The container is unprivileged despite running as root; kernel profiling and
  Docker-in-Docker are unavailable.
- The initial simulation is CPU-first. CUDA 13.2 and an RTX 5080 are present but
  are irrelevant until measured PPO work justifies a backend.
- No service or external port is needed for Gate Zero or Update 01.

## Reproduction commands

See `evidence/gate-zero/raw/baseline-commands.txt` for commands and
`evidence/gate-zero/raw/baseline-results.txt` for captured results. Results after
Gate Zero will naturally include the new docs and must be compared to the
pre-artifact inventory recorded above.

`evidence/gate-zero/raw/baseline-transcript.txt` preserves the literal stdout,
stderr, and exit-state facts from the initial terminal calls separately from the
curated narrative. The environment manifest is structured provenance, not a
benchmark result.
