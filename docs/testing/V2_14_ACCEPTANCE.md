# V2.14 acceptance matrix

Status: implementation in progress

## Automated quality and persistence

- [ ] MSVC Debug and static Release configure and build through Ninja presets.
- [ ] C++ formatting passes.
- [ ] Full clang-tidy passes with warnings as errors.
- [ ] QML formatting and `qmllint` pass.
- [ ] English/Chinese translation parity passes.
- [ ] Complete serial CTest passes in Debug and static Release.
- [ ] Settings, SSH profiles, forwarding rules, scripts, and workspace recover a
      valid last-known-good document after malformed, truncated, and oversized
      primaries.
- [ ] Missing primaries may recover a valid backup; invalid backups do not mask
      the original failure.
- [ ] Unsupported future schemas neither fall back nor get overwritten.
- [ ] Invalid current primaries never replace a valid backup during the next
      save.

## Automated stability budgets

- [ ] The 30-second developer stability gate produces at least six local
      terminal latency windows with P95 input queue time at or below 16 ms.
- [ ] The release local-terminal soak runs for 30 minutes with no latency-growth,
      event-loop starvation, snapshot starvation, or sustained handle growth.
- [ ] Repeated real SSH connect/disconnect finishes every cycle, keeps input P95
      within its existing budget, and returns process handles to the allowed
      bound.
- [ ] SFTP, forwarding, logging, vault, transfer recovery, and application
      shutdown ownership tests pass without detached workers or late callbacks.
- [ ] Application lifecycle runtime shutdown completes in under five seconds.

## Windows runtime and real host

- [ ] All eight serial real-window gates pass: work area, DWM appearance,
      resize/hit-test, DPI 100/125/150/200, responsive layout, keyboard,
      terminal rendering, and lifecycle.
- [ ] Direct key-auth SSH, terminal open/close, SFTP list and transfer, history,
      telemetry, and explicit host-key handling pass against the approved real
      host fixture.
- [ ] ProxyJump/chain, reconnect, forwarding, and recursive-transfer real-host
      paths are run where the fixture supports them; unsupported fixture paths
      are recorded rather than inferred.

## Release artifacts

- [ ] Portable ZIP launches without missing dependencies and keeps data under
      its portable root.
- [ ] MSI structure is inspected as a per-user install with the application
      icon, Start-menu shortcut, same-version upgrade behavior, and uninstall
      component contract.
- [ ] WiX ICE validation is run when Windows Installer is available; otherwise
      only that environmental limitation is explicitly recorded.
- [ ] SHA-256 checksums are recorded for ZIP and MSI.

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
