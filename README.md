# ztermy

ztermy is a Windows 11-first native SSH terminal application built with Qt 6
and C++23.

The project currently has a buildable native window-shell milestone. Netcatty
is the primary product reference for the initial feature set and visual
direction. Its source code, assets, themes, and implementation are not copied.

## Technology

- Qt 6.8+ with Qt Quick and QML for the application shell
- C++23 for application, domain, platform, and terminal integration
- MSVC with the Ninja CMake generator
- Windows ConPTY for local terminal sessions
- Pinned `libghostty-vt` behind a ztermy-owned C++ terminal-engine interface
- A native SSH library for SSH and SFTP
- A single custom Qt Quick terminal item with batched rendering

## Supported platform

- Windows 11 x64

Other platforms are out of scope until the Windows version is stable.

## Project status

The Qt Quick shell, Windows non-client integration, engine-independent ConPTY
transport, and first `libghostty-vt` adapter are implemented. The active
milestone is terminal-session integration and immutable renderer snapshots.
See [docs/V1_SCOPE.md](docs/V1_SCOPE.md) and
[docs/V1_ACCEPTANCE.md](docs/V1_ACCEPTANCE.md).

## Building

The project builds with MSVC through Ninja and exports
`compile_commands.json`. Toolchain requirements and preset commands are
documented in [docs/BUILDING.md](docs/BUILDING.md).

## License

No license has been selected. All rights are reserved until a license file is
added.
