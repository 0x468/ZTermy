# Building

Status: toolchain baseline

## Required tools

- Windows 11 x64
- Visual Studio 2022 with Desktop development with C++
- MSVC x64 toolset
- Windows 11 SDK
- CMake 3.28 or newer for development builds
- CMake 4.3 or newer for the per-user MSI
- Ninja
- Qt 6.8.3 for MSVC 2022
- Zig 0.16.0
- .NET SDK and WiX Toolset 4.0.4 for MSI packaging
- LLVM clangd, clang-format, and clang-tidy 22.1 or newer

## Local Qt installations

The current development machine has:

```text
Dynamic Qt: C:\Qt\6.8.3\msvc2022_64
Static Qt:  D:\qt-self-built\qt-6.8.3-static
```

These paths are not embedded in the shared presets. Set them in the developer
shell:

```powershell
$env:ZTERMY_QT_DYNAMIC_ROOT = "C:\Qt\6.8.3\msvc2022_64"
$env:ZTERMY_QT_STATIC_ROOT = "D:\qt-self-built\qt-6.8.3-static"
```

## MSVC environment

Use an x64 Visual Studio Developer PowerShell before configuring with Ninja.
The compiler, Windows SDK, linker, and library paths must all come from the same
activated VS installation. The root CMake project also corrects a known
Simplified Chinese `/showIncludes` prefix encoding mismatch when CMake detects
that exact condition. Without a matching dependency prefix, Ninja can miss
header-only changes and leave stale object files.

## Intended preset workflow

```powershell
cmake --preset msvc-dynamic-debug
cmake --build --preset msvc-dynamic-debug
ctest --preset msvc-dynamic-debug
```

Release diagnostics:

```powershell
cmake --preset msvc-dynamic-relwithdebinfo
cmake --build --preset msvc-dynamic-relwithdebinfo
```

Dynamic release packaging:

```powershell
cmake --preset msvc-dynamic-release `
  -DZTERMY_WIX_ROOT="$PWD/build/tools/wix"
cmake --build --preset msvc-dynamic-release `
  --target ztermy_portable_contract_smoke
cmake --build --preset msvc-dynamic-release `
  --target ztermy_installer_contract_smoke
```

Static packaging:

```powershell
cmake --preset msvc-static-release
cmake --build --preset msvc-static-release

dotnet tool install --tool-path build/tools/wix wix --version 4.0.4
cmake --preset msvc-static-release `
  -DZTERMY_WIX_ROOT="$PWD/build/tools/wix"
cmake --build --preset msvc-static-release `
  --target ztermy_v2_automated_preflight
```

The current release handoff is recreated below
`build/msvc-static-release/package/release/ztermy-0.2.14-windows-x64`.

The V2 preflight serializes all real-window runtime gates so multiple test
windows never compete for native foreground, DPI, or capture state. It then
runs formatting with `--dry-run --Werror`, analyzes every project translation
unit under `src`, `tests`, and `tools` with all clang-tidy diagnostics treated
as errors, checks all application QML with Qt 6.8 `qmlformat` and `qmllint`,
and runs the complete non-real-host CTest suite. The opt-in real-host test is
excluded by name so an inherited environment cannot contact a host during a
normal release build. Every test executable is an explicit
dependency, so the target cannot accidentally run stale binaries after a
source or header change. In the static preset it also creates the portable ZIP
and creates, validates, decompiles, and inspects the per-user MSI, then
assembles the two validated artifacts with SHA-256 text and JSON manifests
below the versioned release bundle directory. The final executable contract
checks the PE version strings, fixed numeric version, and native icon rather
than trusting CMake configuration alone. The dynamic presets expose the same
target without the static distribution steps.

The quality gates can also be run independently:

```powershell
cmake --build --preset msvc-dynamic-debug --target ztermy_format_check
cmake --build --preset msvc-dynamic-debug --target ztermy_clang_tidy_check
cmake --build --preset msvc-dynamic-debug --target ztermy_qml_quality_check
```

`ztermy_clang_tidy_check` analyzes every translation unit on every invocation,
but Ninja schedules independent files through a bounded pool instead of one
long-lived sequential clang-tidy process. The default is half the detected
physical cores capped at four, limiting the typical analysis working set to
roughly four gigabytes. Override it at configure time when the machine has a
different CPU/RAM balance:

```powershell
cmake --preset msvc-dynamic-debug -DZTERMY_CLANG_TIDY_JOBS=6
```

Use `1` to reproduce the original sequential behavior. This setting changes
only scheduling: diagnostics, `--warnings-as-errors=*`, the compilation
database, and the complete source set remain identical. Real-window release
gates still wait for analysis to finish and continue to run serially.

The checked source set is discovered from `src`, `tests`, and `tools` during
CMake generation. The compilation database for the active preset remains the
source of truth for clang-tidy compiler flags and include paths. The QML format
check formats copies below the build directory and compares hashes, so the
check never edits working-tree files.

The dynamic Qt presets use the DLL MSVC runtime. The static Qt preset uses the
static MSVC runtime because the local static Qt build was compiled that way.
Dependencies must use a matching runtime.

CMake verifies that the dynamic presets resolve a shared `Qt6::Core` and the
static preset resolves a static `Qt6::Core`. Configuration fails instead of
producing a mislabeled package when a Qt root is missing or a stale cache points
at the wrong installation. Use `--fresh` when switching an existing build
directory between Qt installations.

Static and dynamic portable archives are written below their preset-specific
`package/portable` directories. Both contain `portable.flag`, so all runtime
data remains below the extracted directory. The dynamic archive additionally
contains the deployed Qt, QML, plugin, graphics, and OpenSSL runtime files and
is started from its extracted tree by `ztermy_portable_contract_smoke`. See
[testing/DISTRIBUTION.md](testing/DISTRIBUTION.md) for the runtime checks.

Each per-user MSI is written directly below its preset build directory. CPack
uses the workspace-local WiX executable selected through the
`ZTERMY_WIX_ROOT` CMake cache path; WiX does not need to be installed globally.
The contract-smoke target generates and validates the MSI and decompiles it.
The static contract requires one `ztermy.exe` and rejects runtime DLLs. The
dynamic contract requires the executable, core Qt DLLs, the Windows platform
plugin, and OpenSSL crypto runtime. Both reject `portable.flag`, PDBs, Debug Qt
libraries, and Ghostty development payloads.

WiX ICE validation calls the Windows Installer service. On the current
development machine a non-elevated WiX process can report `WIX0217` and exit
217 even while `msiserver` is Running. Run the static preflight from an
elevated x64 MSVC environment when that exact service-access error occurs.
This elevation is a development-time validation requirement only; the
generated per-user MSI remains installable without elevation. If the owner
explicitly accepts that exact environment exception for an RC, configure with
`-DZTERMY_SKIP_ICE_VALIDATION=ON`; `ztermy_installer_contract_smoke` and
`ztermy_release_bundle` then skip only ICE while retaining WiX decompilation and
all structural MSI checks. The option defaults to `OFF`, must not hide a real
ICE schema failure, and requires the exception to be recorded in the RC handoff.

## libghostty-vt

The terminal engine adapter builds `libghostty-vt` from the exact Ghostty
revision `ae8727401d8c549671c36cdc326a94f47c94b635`. CMake verifies the source
archive SHA-256 before extraction. The first configure requires internet
access unless `FETCHCONTENT_SOURCE_DIR_GHOSTTY` points to an existing checkout.

Put Zig 0.16.0 on `PATH`, or pass its executable explicitly:

```powershell
cmake --preset msvc-dynamic-debug -DZIG_EXECUTABLE=C:\tools\zig\zig.exe
```

Use a short global Zig cache path on Windows. Long dependency cache paths can
exceed Windows path limits:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR = "C:\zig-cache"
```

The current Windows build uses `-Dsimd=false`, producing a self-contained
static VT library without the optional SIMD runtime dependencies. Ghostty is
built with `ReleaseFast` in application Debug builds because its Zig Debug
configuration is prohibitively slow. Release presets receive the equivalent
optimization flag from Ghostty's CMake wrapper.

## libssh2

The SSH transport builds the official libssh2 1.11.1 release archive as a
static library. CMake verifies its SHA-256 before extraction. The initial
Windows integration uses WinCNG with Windows 10+ ECDSA support enabled, so the
developer and static release builds do not require a separate OpenSSL runtime.
libssh2 examples, upstream tests, shared libraries, and debug protocol logging
are disabled; ztermy tests exercise the adapter boundary.

The WinCNG choice remains subject to the real-host key-algorithm matrix in
[ADR 0003](adr/0003-libssh2-transport.md). If it cannot cover the modern key
types required for V1, the pinned libssh2 integration will move to a pinned
OpenSSL 3 backend before release.

## clangd

The default clangd compilation database is:

```text
build/msvc-dynamic-debug/compile_commands.json
```

Configure that preset before relying on code diagnostics. Compiler and include
flags come from the compilation database rather than being duplicated in
`.clangd`.

## Current verification

The native window shell, ConPTY transport, and pinned `libghostty-vt` adapter
have been configured and built successfully with both local Qt 6.8.3
installations:

- dynamic Debug: MSVC DLL runtime (`/MDd`)
- static Release: MSVC static runtime (`/MT`)

The `window-hit-test`, `conpty-process`, and `terminal-engine` suites pass in
both configurations.

### Long-running V3 gates

The ordinary test suite keeps bounded timeouts and skips opt-in soak functions.
Release-candidate duration evidence uses dedicated drivers:

```powershell
pwsh -NoProfile -File .\scripts\run_ai_concurrency_soak.ps1 `
  -BuildDirectory .\build\msvc-static-release `
  -DurationSeconds 7200

pwsh -NoProfile -File .\scripts\run_terminal_stability_soak.ps1 `
  -BuildDirectory .\build\msvc-static-release `
  -DurationSeconds 28800
```

Both write content-free JSON reports below the selected build directory. The
terminal driver raises `QTEST_FUNCTION_TIMEOUT` to the requested duration plus
a ten-minute shutdown margin; without that override QtTest intentionally aborts
any single test function after five minutes. A watchdog remains active so a
deadlocked long run still fails.

After both reports and the release bundle exist, verify their duration,
content-free result contracts, exact artifact set, manifest identity, and
SHA-256 digests together:

```powershell
pwsh -NoProfile -File .\scripts\verify_v3_release_candidate.ps1 `
  -BuildDirectory .\build\msvc-static-release
```

The verifier defaults to the `0.3.0` two-hour AI report, eight-hour terminal
report, and packaged Windows x64 handoff. Its version, paths, and minimum
durations are explicit parameters so shorter developer evidence can exercise
the same verifier without being mistaken for release evidence.
