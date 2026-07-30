# ztermy

ztermy is a Windows 11-first native SSH terminal application built with Qt 6
and C++23. The current V1 release candidate is intended for personal use and
awaits its final manual Windows acceptance pass.

Netcatty is the primary product reference for the initial feature set and
visual direction. Its source code, assets, themes, branding, and implementation
are not copied.

## Technology

- Qt 6.8+ with Qt Quick and QML for the application shell
- C++23 for application, domain, platform, and terminal integration
- MSVC with the Ninja CMake generator
- Windows ConPTY for local terminal sessions
- Pinned `libghostty-vt` behind a ztermy-owned C++ terminal-engine interface
- Pinned `libssh2` with OpenSSL 3 for SSH and a future SFTP boundary
- A single custom Qt Quick terminal item with batched rendering

## Supported platform

- Windows 11 x64

Other platforms are out of scope until the Windows version is stable.

## Project status

The V1 implementation and automated gates are complete. The release candidate
includes:

- a fully custom Windows 11 title bar with native resize, Snap Layouts, DPI,
  work-area, theme, opacity, and backdrop integration;
- local PowerShell through ConPTY and SSH terminals with password or
  private-key authentication and strict application-owned host trust;
- saved host management, multiple independent terminal tabs, search,
  selection, clipboard workflows, scrollback, CJK, emoji, and Windows IME;
- persistent appearance and terminal settings, structured diagnostics, a
  static portable package, and a per-user MSI; and
- one checksummed release handoff with machine-readable artifact metadata.

The remaining V1 gates require human evidence for a real password-authenticated
host, physical mixed-DPI movement, visual and keyboard review, unsupported DWM
fallback, installer lifecycle, and a clean Windows 11 environment. See
[docs/V1_SCOPE.md](docs/V1_SCOPE.md),
[docs/V1_ACCEPTANCE.md](docs/V1_ACCEPTANCE.md), and the ordered
[manual acceptance runbook](docs/testing/V1_MANUAL_ACCEPTANCE.md).

## Building

The project builds with MSVC through Ninja and exports
`compile_commands.json`. Toolchain requirements and preset commands are
documented in [docs/BUILDING.md](docs/BUILDING.md). The static preflight
produces the authoritative MSI, portable ZIP, and SHA-256 manifests below its
versioned release-bundle directory.

## License

No license has been selected. All rights are reserved until a license file is
added.
