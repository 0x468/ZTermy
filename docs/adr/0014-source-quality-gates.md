# ADR 0014: Source quality gates

Status: accepted

## Context

The repository already required clangd, clang-format, clang-tidy, Qt QML
tooling, and an MSVC compilation database, but formatting and static analysis
were manual conventions rather than reproducible build targets. The original
clang-tidy header filter matched only relative paths, while the MSVC Ninja
compilation database supplies absolute Windows paths. As a result, project
headers were silently excluded from the intended analysis. QML had no
formatting gate even though it owns all presentation and interaction glue.

The V1 preflight also invoked CTest without explicitly rebuilding every test
executable. After a header change, that could run a stale test binary and
produce evidence for code that was no longer current. Running compilation,
packaging, or multiple real windows alongside a terminal responsiveness gate
would likewise weaken its performance evidence.

## Decision

LLVM clang-format and clang-tidy 22.1 or newer are required on the build
machine. CMake exposes:

- `ztermy_format_check`, which checks all C++ sources and headers under `src`
  and `tests` with the repository format and `--Werror`;
- `ztermy_clang_tidy_check`, which reads the active preset's
  `compile_commands.json`, analyzes every available project translation unit,
  and treats every enabled diagnostic as an error;
- `ztermy_qml_format_check`, which formats isolated copies with Qt's
  `qmlformat` and compares their SHA-256 values with all source QML files;
- `ztermy_qml_quality_check`, which combines the QML format gate with Qt's
  generated `ztermy_qmllint` target and its generated module/type metadata;
- `ztermy_test_binaries`, which explicitly depends on every CTest executable;
- `ztermy_v1_automated_preflight`, which depends on current test and
  distribution artifacts, runs quality gates, executes the seven real-window
  gates serially, and then runs CTest.

The clang-tidy header filter accepts both slash forms and absolute Windows
paths. `portability-avoid-pragma-once` is disabled deliberately because ztermy
is Windows 11/MSVC-only for V1 and consistently uses `#pragma once`; other
portability diagnostics remain enabled. A diagnostic may be suppressed only
at the narrowest justified location with an explanatory comment.

Static distribution work and test compilation must complete before any
real-window gate starts. QML and C++ quality checks also complete before those
gates. The seven window gates are multiple commands in one Ninja console job
so they cannot overlap each other or clang-tidy.

## Consequences

- A normal configure now fails when the verified LLVM tool versions are
  missing or too old.
- Dynamic Debug and static Release can expose different diagnostics, so both
  preflights remain required before V1 sign-off.
- Static analysis increases preflight duration but cannot silently pass with
  stale tests or an incomplete header filter.
- New C++ files below `src` or `tests` enter the quality source set after CMake
  regenerates.
- New QML files in the application module must pass both `qmlformat` and
  `qmllint`; the format comparison operates on build-directory copies so it
  never rewrites source during a check.
- New test executables must be added to `ztermy_test_binaries`.
- Runtime performance evidence is collected only after compilation and static
  package inspection have completed.
