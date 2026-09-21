# ztermy

ztermy is a Windows 11-first native SSH terminal application built with Qt 6
and C++23. The current release line is 0.5.x (`汐`), with native local/SSH
terminals, SFTP, a built-in provider-backed assistant, unified themes, and a
multi-tab/multi-pane workspace.

Netcatty is the primary product reference for the initial feature set and
visual direction. Its source code, assets, themes, branding, and implementation
are not copied.

## Technology

- Qt 6.8+ with Qt Quick and QML for the application shell
- C++23 for application, domain, platform, and terminal integration
- MSVC with the Ninja CMake generator
- Windows ConPTY for local terminal sessions
- Pinned `libghostty-vt` behind a ztermy-owned C++ terminal-engine interface
- Pinned `libssh2` with OpenSSL 3 for SSH and SFTP
- A single custom Qt Quick terminal item with batched rendering

## Supported platform

- Windows 11 x64

Other platforms are out of scope until the Windows version is stable.

## Project status

V1 through V5 have been delivered through 0.5.1. The current product includes:

- a fully custom Windows 11 title bar with native resize, Snap Layouts, DPI,
  work-area, theme, opacity, and backdrop integration;
- local PowerShell through ConPTY and SSH terminals with password or
  private-key authentication and strict application-owned host trust;
- saved host management, multiple independent terminal tabs, search,
  selection, clipboard workflows, scrollback, CJK, emoji, and Windows IME;
- Windows Credential Manager integration plus a password-protected portable
  credential vault with verified migration and cleanup controls;
- persistent appearance and terminal settings, structured diagnostics, a
  static portable package, and a per-user MSI; and
- native Tab/Pane/Session transfers, detachable terminal windows, unified
  application/terminal themes, and the built-in provider-backed assistant.

See [docs/V_SERIES_STATUS.md](docs/V_SERIES_STATUS.md) for the authoritative
series status and current unfinished work. Historical Scope, Program and
Acceptance documents remain evidence for their original milestones.

## Building

The project builds with MSVC through Ninja and exports
`compile_commands.json`. Toolchain requirements and preset commands are
documented in [docs/BUILDING.md](docs/BUILDING.md). The static preflight
produces the authoritative MSI, portable ZIP, and SHA-256 manifests below its
versioned release-bundle directory.

## License

No license has been selected. All rights are reserved until a license file is
added.
