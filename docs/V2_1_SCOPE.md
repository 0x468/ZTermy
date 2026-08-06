# V2.1 daily-use stabilization

Status: release candidate finalized on the `0.2.1` line

## Release identity

V2.1 is a compatible maintenance release over the accepted `0.2.0` baseline.
It retains the V2 codename `此` and the approved verse because ztermy codenames
belong to the `x.y` line, not individual patch builds.

## Priorities

1. Make failures diagnosable without collecting terminal input, credentials,
   host data, command history, log contents, or crash dumps automatically.
2. Harden long-running SSH, SFTP, transfer cancellation, tab close, and app
   shutdown behavior from reproducible daily-use evidence.
3. Improve installer and portable-package diagnostics while preserving the
   existing per-user installation and portable storage contracts.
4. Finish visual consistency using ztermy-owned vector assets and retain the
   English/Chinese, keyboard, and accessibility contracts.

## First slice: privacy-safe diagnostics

The Application settings page can export a small JSON environment summary and
open the local log or crash-report directory. The JSON contains application,
Qt, Windows/kernel, CPU architecture, storage-mode, and artifact count/size/time
metadata only. It never embeds artifact names, local paths, contents, saved
profiles, credentials, terminal data, or command history.

Crash dumps are deliberately excluded because a dump can contain process memory.
The owner must review and choose a dump explicitly before sharing it.

## Second slice: orderly session and transfer shutdown

Application shutdown now requests cancellation across all transfer and SFTP
workers before waiting for any one owner, explicitly flushes per-tab session
logs, and releases worker-owning services while the controller is still alive.
The path is idempotent, rejects new transfer work after stopping begins, and
preserves incomplete transfers as `interrupted` entries for explicit retry on
the next launch.

## Release-candidate scope

The remaining `0.2.1` candidate work is limited to interface visual
consistency and automated release finalization. Real-host lifecycle acceptance
and additional installer or portable-package diagnostics are not release
blockers for this maintenance line.

Icon-only controls and compact indicators use the shared resource-driven SVG
system, preserving dynamic theme colors, high-DPI rendering, keyboard focus,
and accessible names without depending on UI-font glyph metrics.

## Release-candidate evidence

- C++ and QML formatting, `qmllint`, and the full 95-step `clang-tidy` target
  pass on the static Release configuration.
- All 41 automated tests that do not contact a real SSH host pass.
- All seven real-window runtime gates pass, including dark and light themes,
  compact and regular layouts, keyboard interaction, terminal rendering, and
  100%, 125%, 150%, and 200% DPI runs.
- The portable ZIP and per-user MSI are assembled into the release directory
  with matching SHA-256 and JSON manifests.
- WiX ICE contract validation remains unavailable on this workstation because
  the Windows Installer service cannot be accessed. Artifact generation and
  independent release-manifest verification pass; the existing environment
  limitation is accepted for this candidate and is not expanded in V2.1.
