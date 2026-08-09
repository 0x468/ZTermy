# V2.14 acceptance matrix

Status: automated release gate complete; retained owner checks remain documented

## Automated quality and persistence

- [x] MSVC Debug and static Release configure and build through Ninja presets.
- [x] C++ formatting passes.
- [x] Full clang-tidy passes with warnings as errors.
- [x] QML formatting and `qmllint` pass.
- [x] English/Chinese translation parity passes.
- [x] Complete serial CTest passes in Debug and static Release.
- [x] Settings, SSH profiles, forwarding rules, scripts, and workspace recover a
      valid last-known-good document after malformed, truncated, and oversized
      primaries.
- [x] Missing primaries may recover a valid backup; invalid backups do not mask
      the original failure.
- [x] Unsupported future schemas neither fall back nor get overwritten.
- [x] Invalid current primaries never replace a valid backup during the next
      save.

## Automated stability budgets

- [x] The 30-second developer stability gate produces at least six local
      terminal latency windows with P95 input queue time at or below 16 ms.
- [x] The release local-terminal soak runs for 30 minutes with no latency-growth,
      event-loop starvation, snapshot starvation, or sustained handle growth.
- [x] Repeated real SSH connect/disconnect finishes every cycle, keeps input P95
      within its existing budget, and returns process handles to the allowed
      bound.
- [x] SFTP, forwarding, logging, vault, transfer recovery, and application
      shutdown ownership tests pass without detached workers or late callbacks.
- [x] Application lifecycle runtime shutdown completes in under five seconds.

## Windows runtime and real host

- [x] All eight serial real-window gates pass: work area, DWM appearance,
      resize/hit-test, DPI 100/125/150/200, responsive layout, keyboard,
      terminal rendering, and lifecycle.
- [x] Direct key-auth SSH, terminal open/close, SFTP list and transfer, history,
      telemetry, and explicit host-key handling pass against the approved real
      host fixture.
- [x] ProxyJump/chain, reconnect, forwarding, and recursive-transfer real-host
      paths are run where the fixture supports them; unsupported fixture paths
      are recorded rather than inferred.

## Release artifacts

- [x] Portable ZIP launches without missing dependencies and keeps data under
      its portable root.
- [x] MSI structure is inspected as a per-user install with the application
      icon, Start-menu shortcut, same-version upgrade behavior, and uninstall
      component contract.
- [x] WiX ICE validation is run when Windows Installer is available; otherwise
      only that environmental limitation is explicitly recorded.
- [x] SHA-256 checksums are recorded for ZIP and MSI.

## Release evidence

- MSVC dynamic Debug and static Release configured and built through the
  project Ninja presets for `0.2.14`.
- The final serial CTest run passed 61/61 in Debug (52.23 seconds) and 61/61 in
  static Release (42.50 seconds).
- `clang-format`, 134 clang-tidy targets with warnings as errors, 46 QML files,
  `qmllint`, and 1,219/1,219 finished Chinese translations passed.
- The 30-second measured terminal gate produced six windows and 1,426
  interactions. Every window reported P95 = 250 microseconds; the aggregate
  initial and final P95 averages were both 250 microseconds. It delivered 2,165
  snapshots, observed 2,837 event-loop ticks, reduced process handles from 178
  to 175, and stopped in 0 ms.
- The 30-minute static Release terminal soak completed successfully with the
  one-hour QtTest function timeout enabled. Its assertions cover the same
  per-window latency, growth, event-loop, snapshot, handle, and stop budgets.
- Both Debug and static Release passed the approved key-host matrix against
  `testkey@192.168.1.25`: explicit host-key confirmation, direct terminal and
  SFTP, shell history, telemetry, encoding/options, authentication rejection,
  remote close, recursive transfer, forwarding, reconnect, and 20 repeated
  connect/disconnect cycles. The two-layer jump and full bootstrap gates used
  the same fixture. A hidden temporary OpenSSH dynamic forward also passed the
  explicit SOCKS5 proxy gate and was reclaimed in a `finally` block.
- The final static Release executable passed all eight serial native-window
  gates, including the 100%, 125%, 150%, and 200% DPI matrix, plus the
  integrated real-host SSH/SFTP UI gate.
- WiX generated the per-user MSI. ICE01-ICE105 could not access this
  workstation's disabled/unavailable Windows Installer service and returned
  WIX0217/217; the service was not changed. With only ICE explicitly skipped,
  WiX decompilation and structural inspection passed the LocalAppData scope,
  single executable, product icon, Start-menu shortcut, same-version upgrade,
  and uninstall-folder contracts.
- The extracted portable ZIP launched successfully without external Qt DLLs,
  detected `portable.flag`, and created its `data`, `logs`, `crashes`, and
  `notes` state below the extracted root.
- Release bundle:
  `build/msvc-static-release/package/release/ztermy-0.2.14-windows-x64`.
  It contains exactly the portable ZIP, MSI, checksum text, and JSON manifest.
- Portable ZIP: 19,400,135 bytes; SHA-256
  `7e2af3130376c115437f48ee1a542e6dc99d67975ee0983ad96f1c7bbf2ed344`.
- MSI: 15,663,104 bytes; SHA-256
  `52ee1ed1b32a4f009e11ebadd7d562e2ce03f1fe7ef8e94c55665827e5d84d14`.

## Retained human checks

- [ ] Keyboard-only traversal covers Hosts, profile editor, Settings, terminal
      toolbar/workbench, SFTP, transfer center, scripts, notes, dialogs, and
      portable-vault unlock without a focus trap.
- [ ] Narrator announces primary controls, selected tabs, validation errors,
      progress, and recovery messages with meaningful names.
- [ ] Light, dark, and system themes retain readable hover/focus/disabled/error
      states at narrow and wide layouts.
- [ ] Chinese and English layouts have no clipped primary action or untranslated
      production string.
- [ ] Physical mixed-DPI/multi-monitor movement, Snap Layouts, maximize/restore,
      resize cursors, Mica/Acrylic/transparent backgrounds, and IME remain
      visually correct.
- [ ] Upgrade from an existing `0.2.x` install preserves settings, profiles,
      scripts, notes, forwarding rules, workspace state, and credentials; an
      uninstall leaves only intentional user data.
- [ ] A deliberately damaged non-secret primary shows a recovery warning and
      restores the previous valid generation without exposing its contents.

Environment-dependent checks may be explicitly accepted or waived by the owner.
Unchecked items are not evidence of execution.
