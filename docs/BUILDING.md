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
activated VS installation.

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

## clangd

The default clangd compilation database is:

```text
build/msvc-dynamic-debug/compile_commands.json
```

Configure that preset before relying on code diagnostics. Compiler and include
flags come from the compilation database rather than being duplicated in
`.clangd`.

## Current verification

The native window-shell milestone has been configured and built successfully
with both local Qt 6.8.3 installations:

- dynamic Debug: MSVC DLL runtime (`/MDd`)
- static Release: MSVC static runtime (`/MT`)

The `window-hit-test` suite passes in both configurations.
