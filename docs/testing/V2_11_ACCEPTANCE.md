# V2.11 acceptance: terminal workspace continuity

This matrix covers the bounded multi-pane terminal workspace introduced in
`0.2.11`. A top-level terminal tab is one workspace; every leaf remains one
custom `TerminalItem` backed by one independently owned terminal session.

## Automated evidence

- Domain tests cover bounded binary split creation, close/collapse, active-pane
  focus, ratio limits, ordering, swap, duplicate identifiers, cycle rejection,
  and the eight-pane/fifteen-node limits.
- Workspace-state schema v6 tests cover atomic topology/ratio/active-pane
  round-trip, v5 migration, unsupported and malformed data rejection, and the
  restore-intent boundary.
- Controller tests cover one public tab with multiple sessions, horizontal and
  vertical split, focus cycling, keyboard ratio changes, swap, close, fresh
  local-shell restoration, and orderly shutdown of every owned session.
- QML compilation, `qmllint`, `qmlformat`, icon contracts, English/Chinese
  translation completeness, and dynamic Loader recursion checks cover the
  terminal workspace UI.
- The terminal render runtime gate opens two real ConPTY-backed PowerShell
  panes, proves independent output, changes and persists a divider ratio,
  changes focus, closes a pane, and captures the resulting workspace.

## Restoration and security evidence

- Local panes restore as fresh local processes; no process-survival claim or
  terminal screen/scrollback serialization is made.
- Saved-profile SSH panes restore disconnected and require explicit reconnect;
  startup restoration does not open the network or persist credentials.
- Unsaved quick-connect panes use transient restore intents and are omitted
  from persisted reconnect state.
- Passwords, passphrases, private-key contents, terminal input, screen content,
  selection, IME state, and remote process state are not serialized.

## Manual acceptance retained

1. Split a local terminal horizontally and vertically until four panes exist.
   Verify every pane accepts independent input, owns its scrollbar, and keeps
   focus, cursor, selection, IME composition, search, and clipboard behavior.
2. Drag each divider to both limits and resize the window rapidly. Panes must
   retain usable minimum geometry, never produce negative terminal sizes, and
   reflow without a crash, stale hit region, or permanently blank surface.
3. Exercise the default focus, split, duplicate, grow/shrink, swap, and close
   shortcuts. Rebind each action in Settings and verify conflict detection,
   repeat behavior, accessibility focus, and English/Chinese labels.
4. Close ztermy with a two-pane local workspace, restart it, and verify the
   same topology, ratio, active pane, and tab order return with fresh shells.
5. Repeat with saved password, private-key, jump-host, and proxy SSH profiles.
   Restart must show disconnected placeholders and make no connection until
   Reconnect is invoked; locked portable credentials must request vault unlock.
6. Open an unsaved quick SSH connection, close ztermy, and restart. It must not
   return as a reconnectable pane or leave its endpoint/secret in workspace
   persistence or logs.
7. At the eight-pane limit, another split/duplicate request must fail visibly
   without changing layout. Close nested first/middle/last panes and verify
   parents collapse deterministically and no session worker remains.
8. Inspect single-, two-, four-, and eight-pane layouts in light/dark themes,
   English/Chinese, compact/regular widths, and 100%, 125%, 150%, and 200% DPI.
9. Exit while local and SSH panes are connecting, reconnecting, producing
   output, searching, and resizing. Shutdown must join every worker without a
   late prompt, heap assertion, crash report, or lingering child process.

## Release evidence

- MSVC dynamic Debug and static Release builds completed successfully.
- All 50 CTest cases passed in dynamic Debug (43.09 seconds) and static Release
  (40.92 seconds). The final controller-only rerun also passed after adding the
  explicit no-network saved-SSH restore assertion.
- `clang-format`, all 111 project clang-tidy translation units, `qmlformat`,
  `qmllint`, the 51-icon contract, and 1,051/1,051 finished Chinese
  translations passed.
- The serial eight-gate static Release real-window preflight passed, including
  the 100%, 125%, 150%, and 200% DPI matrix and two-live-pane ConPTY render,
  focus, ratio, and close evidence.
- The authorized `testkey` real-host controller gate passed saved-profile host
  key verification, explicit reconnect, bounded retry, and deterministic
  shutdown against the trusted fixture.
- Portable extraction lifecycle smoke passed. The self-contained artifacts are
  under `build/msvc-static-release/package/release/ztermy-0.2.11-windows-x64`:
  - portable SHA-256:
    `330458fbf1f49cfc800477abf1126774e2adb3c599c077e697926532380c325d`;
  - MSI SHA-256:
    `e11d2ce1f4129c8a86065d45424c64d34654aaaa04d6ced2eb1894eeb4477d77`.
- WiX produced the MSI, but ICE contract validation could not run because the
  Windows Installer service remains disabled/unavailable on this workstation.
  The service was not changed; install/upgrade/uninstall stays in the retained
  manual matrix.
