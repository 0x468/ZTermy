# V2 release record: 8f8fccf

Status: accepted as the V2 0.2.0 personal/private baseline

## Candidate identity

| Field | Value |
| --- | --- |
| Date | 2026-08-03 |
| Binary source commit | `8f8fccf9b561da0aa34c247f4bfde1fad8abaf8c` |
| Version | `0.2.0` |
| Platform | Windows 11 x64 |
| Qt | 6.8.3 static Release and dynamic Debug, MSVC 2022 |
| Release bundle | `build/msvc-static-release/package/release/ztermy-0.2.0-windows-x64` |
| Tester | Project owner and automated release gates |

The documentation commit and signed tag containing this record are not part of
the binaries. The source commit and hashes below identify the exact artifacts.

## Automated evidence

The binary source commit passed `ztermy_v2_automated_preflight` independently
in dynamic Debug and static Release:

- all 34 QML files passed `qmlformat` and `qmllint`, C++ formatting passed, and
  all 722 Simplified Chinese translations were finished;
- all 87 project translation units passed clang-tidy with diagnostics treated
  as errors;
- all 37 non-real-host CTest tests passed in each configuration;
- all seven real-window gates passed, including work-area maximize, native
  resize, appearance materials, responsive layout, keyboard interaction,
  terminal rendering, and synthetic 100%, 125%, 150%, and 200% DPI;
- the static run rebuilt the portable archive and per-user MSI, passed the MSI
  payload/shortcut/uninstall contract, and assembled the four-file release
  bundle;
- WiX ICE validation completed with only the reviewed ICE61, ICE69, and ICE91
  warnings documented in ADR 0011.

On this development machine WiX ICE could not access Windows Installer from a
non-elevated process even while `msiserver` reported Running. The identical
contract target and final full static preflight passed from an elevated MSVC
environment. MSI creation itself succeeded in both cases. The failed
intermediate artifacts were replaced by the final successful run and are not
release artifacts.

The normal release gate deliberately excluded the opt-in `ssh-real-host` test
by exact name. Real password and `id_ed25519` authentication, host-key trust,
and terminal/SFTP operation had already been exercised during the accepted V1
and V2 development passes without recording credentials.

## Artifact integrity

| Artifact | Bytes | Independently calculated SHA-256 |
| --- | ---: | --- |
| `ztermy-0.2.0-windows-x64-portable.zip` | 18,624,693 | `c24cc5fdddf24459843c9b488ab4313ec827c2aa4cdeadc18d718a5b38a8bc12` |
| `ztermy-0.2.0-windows-x64.msi` | 15,376,384 | `f08045850351488d7c22bcf4c049b37baf8543e6b63ab642a7efa7608dad4371` |

Both values exactly match `SHA256SUMS.txt` and `release-manifest.json`. The
handoff directory contains only those two artifacts and the two manifests.

## Owner acceptance

The owner iteratively exercised the Windows 11 window shell, terminal, SSH,
credentials, appearance, localization, workbench, SFTP, transfers, portable
mode, and MSI/Sandbox routes throughout V1 and V2 development. On 2026-08-03
the owner reported the final candidate normal after verifying the last repair
set, including:

- cancelling an active transfer remains actionable instead of being replaced
  by the progress refresh;
- the direct terminal SFTP action opens its panel on a connected SSH session;
- existing-group completion permits repeated Backspace and re-completion;
- the current toolbar, menu, SFTP, and transfer flows no longer reproduce the
  reported candidate regressions.

This acceptance makes `0.2.0` the immutable personal/private daily-use V2
baseline. It does not claim that every environment in
[DEFERRED_MANUAL_ACCEPTANCE.md](DEFERRED_MANUAL_ACCEPTANCE.md) was available.
Physical mixed-DPI/multi-monitor transitions, the full Windows contrast-theme
and Narrator matrix, and every destructive shutdown timing combination remain
visible regression work rather than falsely reported passes. There are no
known release-blocking crashes, secret leaks, or data-loss defects in the
accepted routes.

No license file is added and no public compatibility promise is made; the
project owner has not selected a license or approved a public release.
