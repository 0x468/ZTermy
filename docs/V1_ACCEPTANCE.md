# V1 acceptance

Status: draft

Only runtime evidence can mark a platform or UI item complete.

## Current milestone evidence

- MSVC + Ninja dynamic Debug build succeeds.
- MSVC + Ninja static Qt Release build succeeds.
- The static Release executable has no Qt or OpenSSL DLL dependency and the
  versioned portable ZIP target succeeds.
- The extracted portable candidate starts through the native/QML smoke path,
  selects `storageMode=portable`, and creates logs and crash diagnostics only
  below its sibling `data` directory.
- The per-user MSI target succeeds. WiX ICE validation completes with only the
  three reviewed ICE61, ICE69, and ICE91 warnings, and decompilation confirms
  a single `ztermy.exe` payload under `LocalAppDataFolder` plus the Start menu
  shortcut.
- The dynamic RelWithDebInfo deployment target installs its Qt, QML, compiler,
  and OpenSSL runtime dependencies into a clean directory. Its native/QML
  smoke path passes with `PATH` restricted to Windows system directories and
  writes diagnostics only below the supplied deployment smoke-data directory.
- Native window creation and QML loading succeed on Windows 11.
- Automated hit-test coverage passes for caption, maximize button, client
  area, all resize edges/corners, and maximized-state behavior.
- The real-window maximized-work-area gate passes in dynamic Debug and static
  Release on the primary display: the client screen rectangle and Win32
  monitor work area both measured `(0, 0)-(2560, 1392)`, followed by a
  successful restore.
- Custom caption commands, maximize-button Snap Layout hover, and Win+Z were
  manually verified on the primary Windows 11 development machine.
- A Debug runtime session displayed a real PowerShell prompt through ConPTY,
  `libghostty-vt`, an immutable cell snapshot, and the custom terminal item
  without a scene-graph crash.
- Automated terminal-engine coverage verifies true color, primary/alternate
  screen restoration, erase and cursor visibility/style, resize, wide CJK
  cells, combining graphemes, and emoji cell widths. Terminal-item coverage
  verifies wide IME carets, suffix displacement, single commit delivery, and
  composition behavior across resize and shutdown.
- Automated end-to-end coverage verifies PowerShell startup, queued input,
  parsed sentinel output, and session shutdown.
- The opt-in local ConPTY input gate processed 120 events at 5 ms intervals.
  Dynamic Debug measured P95 `100 us`, P99 `250 us`, and maximum `298 us`;
  static Release measured P95 `250 us`, P99 `500 us`, and maximum `490 us`.
  Both are below the 16 ms V1 queue-latency target.
- The opt-in local large-output gate processed 20,000 PowerShell lines in
  `1992 ms` in both builds. Dynamic Debug delivered 157 progressive snapshots
  while a 10 ms Qt heartbeat fired 178 times; static Release delivered 244
  snapshots and 186 heartbeats. Both sessions stopped in under 1 ms without
  starving the Qt event loop.
- Automated SSH coverage preserves distinct failure state and user-visible
  status contracts for name resolution, refusal, timeout, transport, host-key,
  authentication, channel, remote-close, cancellation, and protocol failures.
- The opt-in real-host SSH input gate processed 120 events at 5 ms intervals.
  Dynamic Debug measured P95 `250 us`, P99 `250 us`, and maximum `182 us`;
  static Release measured P95 `100 us`, P99 `250 us`, and maximum `126 us`.
  Both runs used the interruptible command wake path and remained below the
  16 ms V1 application-side queue-latency target.
- Runtime credential-hygiene coverage sends unique password and passphrase
  sentinels into failing SSH worker requests and verifies that neither emitted
  statuses nor captured Qt logs contain them. Profile persistence coverage
  verifies that password, passphrase, and private-key content fields are absent
  from saved configuration.
- The opt-in real-host lifecycle gate completed an initial verified-host
  warm-up followed by 20 private-key authenticated connect/disconnect cycles
  against the Windows 11 development host, with every worker reaching
  `Disconnected` and no linear process-handle growth.
- The no-credential real-host gate observed an unknown public host key before
  authentication, kept authentication closed for a temporary same-endpoint
  changed-key record, and opened it only for the exact observed key. The
  temporary record was not persisted.

Platform and UI checkboxes remain open until the corresponding behavior has
been manually exercised across the required Windows 11 and mixed-DPI scenarios.
Use [testing/WINDOW_SHELL.md](testing/WINDOW_SHELL.md) and
[testing/TERMINAL_SESSION.md](testing/TERMINAL_SESSION.md), and
[testing/HOST_VAULT.md](testing/HOST_VAULT.md), and
[testing/TERMINAL_TABS_SEARCH.md](testing/TERMINAL_TABS_SEARCH.md), and
[testing/UI_SHELL.md](testing/UI_SHELL.md), and
[testing/APPEARANCE_SETTINGS.md](testing/APPEARANCE_SETTINGS.md), and
[testing/DISTRIBUTION.md](testing/DISTRIBUTION.md) for the repeatable runtime
procedures and expected results.

## Window shell

- [x] Custom title bar preserves minimize, maximize, restore, and close
- [x] Hovering maximize/restore shows Windows 11 Snap Layouts
- [x] Win+Z and snap keyboard shortcuts work
- [ ] All edges and corners resize with native cursors
- [x] Double-clicking draggable title space toggles maximize/restore
- [x] Maximized window respects the monitor work area
- [ ] Moving between mixed-DPI monitors preserves geometry and sharp rendering
- [ ] Dark/light DWM integration is correct
- [ ] Opacity and backdrop settings fail safely when unsupported

## Terminal

- [x] Local input dispatch P95 is below 16 ms
- [x] SSH input adds no application-side batching delay
- [ ] Large output does not freeze the window
- [ ] ANSI colors, alternate screen, cursor, clear, and resize are correct
- [ ] CJK, wide characters, combining marks, emoji, and IME are correct
- [ ] Selection, copy, paste, search, and scrollback are stable
- [ ] Thirty minutes of interaction shows no growing latency

## SSH security and reliability

- [ ] Password authentication passes on a real host
- [x] Private-key authentication passes on a real host
- [x] Unknown host keys require confirmation before authentication
- [x] Changed host keys block the connection before authentication
- [ ] Authentication failure, timeout, refusal, and remote close are distinct
- [x] Twenty connect/disconnect cycles leave no workers or handles behind
- [x] Logs and configuration contain no credentials

## Distribution

- [x] Dynamic developer build runs from a clean deployment directory
- [x] Static release build starts without Qt DLLs
- [x] Portable data remains inside the portable directory
- [ ] Installed data survives upgrade and uninstall
- [ ] A clean Windows 11 machine runs the release without developer tools
