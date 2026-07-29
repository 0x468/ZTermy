# Building

Status: toolchain baseline

## Required tools

- Windows 11 x64
- Visual Studio 2022 with Desktop development with C++
- MSVC x64 toolset
- Windows 11 SDK
- CMake 3.28 or newer
- Ninja
- Qt 6.8.3 for MSVC 2022
- Zig 0.16.0
- LLVM clangd, clang-format, and clang-tidy

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

Static packaging:

```powershell
cmake --preset msvc-static-release
cmake --build --preset msvc-static-release
ctest --test-dir build/msvc-static-release --output-on-failure
```

The dynamic Qt presets use the DLL MSVC runtime. The static Qt preset uses the
static MSVC runtime because the local static Qt build was compiled that way.
Dependencies must use a matching runtime.

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
