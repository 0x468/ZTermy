# V1 acceptance

Status: release candidate; manual sign-off pending

Only runtime evidence can mark a platform or UI item complete.

## Current milestone evidence

- MSVC + Ninja dynamic Debug build succeeds.
- MSVC + Ninja static Qt Release build succeeds.
- The static Release executable has no Qt or OpenSSL DLL dependency and the
  versioned portable ZIP target succeeds.
- The extracted portable candidate starts through the native/QML smoke path,
  selects `storageMode=portable`, and creates logs and crash diagnostics only
  below its sibling `data` directory.
- The per-user MSI contract target succeeds. WiX ICE validation completes with
  only the three reviewed ICE61, ICE69, and ICE91 warnings. Automated
  decompilation requires per-user scope, `LocalAppDataFolder`, a direct Start
  menu shortcut, same-version upgrade support, uninstall directory removal,
  Installed Apps product icon, and exactly one non-empty `ztermy.exe` payload;
  it rejects portable markers, DLLs, PDBs, and Ghostty development files.
- The static release-bundle target recreates one versioned handoff directory
  containing only the validated portable ZIP, validated MSI,
  `SHA256SUMS.txt`, and `release-manifest.json`. The JSON manifest identifies
  Windows x64 version `0.1.0`, and both manifests contain the exact copied
  artifact digests.
- The unified V1 automated preflight passes in dynamic Debug and static
  Release. Both builds pass clang-format with `--Werror` and all 44 project
  translation units pass LLVM 22.1 clang-tidy with every enabled diagnostic
  treated as an error. All 21 application QML files match Qt 6.8 `qmlformat`
  and pass the generated `qmllint` module check. Every test executable is
  rebuilt before the seven real-window window, DPI, UI, keyboard, and
  terminal-render gates run serially, followed by all 20 CTest tests. The
  static run additionally regenerates the portable ZIP, completes MSI
  generation, ICE validation, decompilation, and payload-contract checks, and
  assembles the checksummed release bundle before the runtime gates begin.
- The executable identity contract reads the finished PE through Win32 APIs.
  Dynamic Debug and static Release expose version `0.1.0`, the ztermy product,
  description and filename fields, and a loadable ztermy-owned multi-size
  application icon. The application, portable archive, and MSI versions all
  derive from the CMake project version.
- The dynamic RelWithDebInfo deployment target installs its Qt, QML, compiler,
  and OpenSSL runtime dependencies into a clean directory. Its native/QML
  smoke path passes with `PATH` restricted to Windows system directories and
  writes diagnostics only below the supplied deployment smoke-data directory.
- Native window creation and QML loading succeed on Windows 11.
- The real-window responsive-layout gate passes at `500x360` and `1120x800`
  in dynamic Debug and static Release. Hosts and Settings switch between one-
  and two-column layouts, host content remains within its page, and eight
  captured Dark/Light views were reviewed for top-level clipping, shared
  control contrast, and caption-icon repainting after live theme changes.
- The real-window keyboard gate passes in dynamic Debug and static Release.
  It verifies eight custom actions through Windows accessibility interfaces,
  24 Settings and 22 host-editor Tab stops across regular and compact layouts,
  Space/Enter/arrow/Alt+Down/Escape control behavior, persisted settings,
  private-key passphrase disclosure state, exactly one new local session, and
  session-preserving navigation back to Hosts.
- Automated hit-test coverage passes for caption, maximize button, client
  area, all resize edges/corners, and maximized-state behavior.
- The real-window maximized-work-area gate passes in dynamic Debug and static
  Release on the primary display: the client screen rectangle and Win32
  monitor work area both measured `(0, 0)-(2560, 1392)`, followed by a
  successful restore.
- The real-HWND resize gate passes in dynamic Debug and static Release. All
  four edges and four corners return the exact Win32 resize hit codes and
  select the matching horizontal, vertical, or diagonal system cursor.
  Maximized checks at the new client bounds expose no resize hit, followed by
  a successful restore.
- The real-window DPI matrix passes in dynamic Debug and static Release at
  100%, 125%, 150%, and 200%. An `800x600` logical client produced exact
  `800x600`, `1000x750`, `1200x900`, and `1600x1200` native and captured pixel
  sizes respectively. Every capture retained its matching device-pixel ratio,
  and the custom maximize button returned native `HTMAXBUTTON` at every scale.
  Four Debug captures were reviewed for stable logical layout and clipping.
- Custom caption commands, maximize-button Snap Layout hover, and Win+Z were
  manually verified on the primary Windows 11 development machine.
- A Debug runtime session displayed a real PowerShell prompt through ConPTY,
  `libghostty-vt`, an immutable cell snapshot, and the custom terminal item
  without a scene-graph crash.
- Automated terminal-engine coverage verifies true color, primary/alternate
  screen and cursor restoration, erase and cursor visibility/style, resize,
  wide CJK cells, atomic wide-cell selection, combining graphemes, and emoji
  cell widths. Terminal-item coverage verifies IME suffix displacement, a
  visible insertion caret, single commit delivery, and composition behavior
  across resize and shutdown. It also verifies inverted block-cursor glyphs,
  exclusive copy/paste shortcut routing, multiline confirmation, normal text
  input, linear and rectangular drag selection, click-to-clear,
  copy-on-select, accumulated high-resolution wheel scrolling, and absolute
  scrollbar navigation. A real Qt Quick window capture verifies final pixels
  for styled and full-width selected backgrounds, an inverted two-cell block
  cursor, and a CJK glyph extending into its trailing cell.
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
- The real-window renderer gate displayed all 20,000 PowerShell lines through
  Qt Quick, resized twice during output, and captured line 20000, the unique
  completion marker, the returned prompt, and a working history scrollbar.
  Dynamic Debug completed in `2995 ms` with 157 heartbeat ticks, a `57 ms`
  maximum heartbeat gap, and 83 swapped frames. Static Release completed in
  `1825 ms` with 211 heartbeat ticks, a `15 ms` maximum gap, and 250 swapped
  frames. Both rendered terminal pixels and remained within the 250 ms
  responsiveness ceiling.
- The real-window appearance gate passed in dynamic Debug and static Release.
  DWM readback matched immersive Dark and Light mode, rounded corners, and
  None/Mica/Acrylic backdrop values `1`/`2`/`3`; Qt readback matched opacity
  `1.0`/`0.85`/`0.75`. QML surface readback additionally verified that None
  remains opaque, Mica and Acrylic expose translucent application surfaces,
  and returning to None restores opacity. Invalid opacity and backdrop values
  were rejected without changing the established state, and the gate restored
  Dark/None/100%.
- The static Release sustained-interaction gate ran a real PowerShell/ConPTY
  session for `1800 s` across 30 independent one-minute latency windows and
  85,563 edit interactions. Every window contained 2,850-2,854 samples and
  measured P95 `100 us`; the initial-third and final-third averages were both
  `100 us`. The Qt heartbeat fired 171,598 times, 131,751 progressive
  snapshots were delivered, the process handle count fell from 171 to 168
  after shutdown, and stopping took under 1 ms.
- Automated SSH coverage preserves distinct failure state and user-visible
  status contracts for name resolution, refusal, timeout, transport, host-key,
  authentication, channel, remote-close, cancellation, and protocol failures.
- The full application-session gate verifies those contracts at live I/O
  boundaries in both dynamic Debug and static Release. A closed ephemeral
  localhost port reports connection refusal, a connected silent peer reports
  handshake timeout, an intentionally unknown username on the authorized
  private-key fixture reports authentication rejection, and a successful
  shell followed by `exit` reports remote close. All four paths produced their
  exact distinct status text without exposing credentials or terminal data.
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
- The real-host interactive password gate passed against the authorized
  Windows 11 fixture in the static Release build. Input remained hidden and
  QtTest reported `3 passed, 0 failed` without exposing the credential.

Platform and UI checkboxes remain open until the corresponding behavior has
been manually exercised across the required Windows 11 and mixed-DPI scenarios.
Use [testing/V1_MANUAL_ACCEPTANCE.md](testing/V1_MANUAL_ACCEPTANCE.md) as the
ordered sign-off and evidence record. It links to the detailed procedures in
[testing/WINDOW_SHELL.md](testing/WINDOW_SHELL.md),
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
- [x] All edges and corners resize with native cursors
- [x] Double-clicking draggable title space toggles maximize/restore
- [x] Maximized window respects the monitor work area
- [ ] Moving between mixed-DPI monitors preserves geometry and sharp rendering
- [x] Dark/light DWM integration is correct
- [ ] Opacity and backdrop settings fail safely when unsupported

## UI convergence

- [x] Primary screens use the shared semantic theme and component foundations
- [x] Hosts and Settings adapt structurally at minimum and regular widths
- [ ] Narrow and regular workflows pass mouse and keyboard visual inspection
- [ ] Dark, Light, and System themes pass contrast and component-state review
- [ ] Primary screens pass the complete keyboard-only accessibility route
- [ ] Normal, snapped, maximized, and mixed-DPI screenshots are accepted

## Terminal

- [x] Local input dispatch P95 is below 16 ms
- [x] SSH input adds no application-side batching delay
- [x] Large output does not freeze the window
- [ ] ANSI colors, alternate screen, cursor, clear, and resize are correct
- [ ] CJK, wide characters, combining marks, emoji, and IME are correct
- [ ] Selection, copy, paste, search, and scrollback are stable
- [x] Thirty minutes of interaction shows no growing latency

## SSH security and reliability

- [x] Password authentication passes on a real host
- [x] Private-key authentication passes on a real host
- [x] Unknown host keys require confirmation before authentication
- [x] Changed host keys block the connection before authentication
- [x] Authentication failure, timeout, refusal, and remote close are distinct
- [x] Twenty connect/disconnect cycles leave no workers or handles behind
- [x] Logs and configuration contain no credentials

## Distribution

- [x] Dynamic developer build runs from a clean deployment directory
- [x] Static release build starts without Qt DLLs
- [x] Portable data remains inside the portable directory
- [ ] Installed data survives upgrade and uninstall
- [ ] A clean Windows 11 machine runs the release without developer tools
