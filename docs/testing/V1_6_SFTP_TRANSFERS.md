# V1.6 SFTP and transfers verification

Date: 2026-08-02

Status: engineering gates passed; owner-visible and real-host acceptance is
deferred in `DEFERRED_MANUAL_ACCEPTANCE.md`.

## Automated evidence

- Dynamic Debug application build passed with MSVC, CMake, and Ninja.
- C++ and QML format gates passed.
- QML lint completed with zero warnings.
- The localization gate verified all 656 source strings and translation call
  sites with no unfinished Chinese translations.
- Full clang-tidy passed for 80/80 compilation units with every diagnostic
  treated as an error.
- Dynamic Debug safe test set: 32/32 passed in 16.34 seconds.
- Static Release full build passed against the project-owned static Qt 6.8.3
  installation.
- Static Release safe test set: 32/32 passed in 23.14 seconds.
- Both safe test runs intentionally excluded `ssh-real-host`,
  `ssh-terminal-session`, and `qml-native-window-smoke`; the first two could
  access a real host when opt-in environment variables are present, and the
  last one creates a real window.
- Synthetic SFTP and transfer coverage includes path normalization, queue state
  transitions, independent session cancellation, stale directory responses,
  progress, conflicts, retry, credential failures, host-key decisions, and
  concurrent credential-vault access.

## Package evidence

The Static Release pipeline generated and inspected:

- `ztermy-0.1.0-windows-x64-portable.zip`
  - SHA-256: `fbb183402ad82f7cb5902a8b77fde77a691a90911e01feb38d5ed7f27c98752e`
- `ztermy-0.1.0-windows-x64.msi`
  - SHA-256: `04f04924e268314ff876e53d5c6bf07a0933219f8e6712705f2fbef548587fd7`

WiX validation and the repository installer-contract inspection passed outside
the restricted command sandbox. The contract confirms a per-user LocalAppData
package with one `ztermy.exe`, the product icon, Start-menu shortcut,
same-version upgrade support, and uninstall-folder removal. The reviewed CPack
ICE61, ICE69, and ICE91 warnings remain as documented by ADR 0011.

The checksummed release bundle is under
`build/msvc-static-release/package/release/ztermy-0.1.0-windows-x64`.

## Deferred acceptance

No ztermy or NetCatty GUI was launched, no Computer Use action was taken, and
no real SSH host was contacted during this gate. Exact manual steps and
expected results remain `PENDING` in `DEFERRED_MANUAL_ACCEPTANCE.md`.
