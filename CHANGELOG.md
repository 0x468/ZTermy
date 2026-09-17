# Changelog

All notable changes will be documented in this file.

The project has not published a release.

## Unreleased

### Performance pass — 2026-09-18

- Build terminal snapshots on the delivery cadence instead of per read. Every
  ConPTY/SSH read used to build a full `libghostty` snapshot even though only
  the latest one per 8 ms delivery slot reached the GUI. Reads now only mark
  the engine dirty; the delivery timer asks the write worker for the next
  build once the pending frame has gone out, and the exit frame is built
  before `processExitObserved`. Key, input and paste bytes are handed to the
  PTY before the snapshot build. A 20k-line PowerShell burst drops from about
  5800 snapshot builds (140 delivered) to about 140 builds per run, removing
  roughly one second of engine-thread CPU per burst; GUI-side medians are
  unchanged (completion 1575 → 1579 ms, paint P95 ≤ 4 ms). AI frame tracking
  now reads snapshot cells directly instead of building and splitting a
  `QString` per frame. `local-terminal-session` gains a regression test that
  asserts at most one snapshot is built per delivery.

### 0.4.6 local validation build — 2026-09-15

- Keep the maximized state when a window is minimized and later restored from
  the taskbar, the tray, single-instance activation or a workspace transfer.
  `QWindow::showMinimized()`/`showNormal()` replaced the whole state set and
  cleared the native `WPF_RESTORETOMAXIMIZED` flag.
- Introduce `ztermy::windowing` (`WindowStateTransitions`, `WindowPresenter`)
  and the `WindowControl` QML singleton as the single owner of minimize,
  maximize/restore and present transitions; all title-bar, detached-window,
  tray, activation and workspace callers route through it (ADR 0051).
- Extend the code structure gate to reject direct `show*()`/`setWindowStates()`
  calls outside `src/core/windowing` and named runtime-smoke helpers.
- Add offscreen `window-state` and `application-instance` tests, and extend
  `ztermy_window_runtime_smoke` with a maximize/minimize/present round trip
  that checks `IsIconic`, `IsZoomed` and `WPF_RESTORETOMAXIMIZED`.
- Show newly detached windows during workspace synchronization without an
  explicit raise; only explicit present actions raise and focus a window.
- Repair `ztermy_terminal_render_runtime_smoke` and
  `ztermy_lifecycle_runtime_smoke` after the on-demand terminal page and the
  hover-gated pane drag capture: synthetic drags now hover before pressing and
  drop inside the target viewport, and the lifecycle smoke opens the terminal
  page before looking for a viewport. The lifecycle smoke now reaches its
  close-latency measurement and reports a tab close above its 3 s budget.
- Give detached windows the main window's native window flags instead of
  `FramelessWindowHint`; the chrome's native event filter already removes the
  frame, and a frameless Qt window is only moved to the work area on maximize,
  so `IsZoomed`, minimize and restore-to-maximized never matched the Qt state.
  `ztermy_terminal_render_runtime_smoke` now drives a detached window through
  its QML caption buttons (maximize, minimize, present, restore) and checks
  `IsZoomed`, `IsIconic` and `WPF_RESTORETOMAXIMIZED`.

### 0.4.5 local validation build — 2026-09-13

- Align detached-window minimize, maximize and close controls flush with the
  window's top/right edges, without inherited pane-toolbar gaps.
- Restrict the persistent pane-drag mouse capture to pane headers and active
  drags so normal title-bar controls receive hover events again.
- Restore visible hover and pressed backgrounds for title-bar tool actions,
  retaining keyboard focus feedback.
- Compile-only handoff for owner verification; no automated tests, tag or release
  publication performed for this build.

### Added

- Persistent terminal workspaces with bounded horizontal and vertical split
  trees, independent native terminal sessions, keyboard focus/resize/swap,
  pane duplication and close, ratio persistence, fresh local restoration, and
  explicit no-network SSH reconnect placeholders
- Initial Qt Quick application shell and dark terminal-oriented visual baseline
- Windows 11 custom title bar with native non-client hit testing
- Snap Layout-compatible maximize-button hit region
- Native resize edges, work-area maximize constraints, DWM dark mode, rounded
  corners, and system backdrop integration
- Unit tests for window hit-test classification
- Engine-independent Windows ConPTY process transport with UTF-8 pipe I/O,
  resize, process waiting, and deterministic cleanup
- ConPTY integration tests covering invalid terminal sizes and bidirectional
  `cmd.exe` communication
- Terminal-engine research and a spike decision record comparing
  `libghostty-vt`, Contour, Windows Terminal, and libvterm
- Pinned `libghostty-vt` C ABI integration behind a replaceable C++23 terminal
  engine interface
- Terminal-engine tests for invalid geometry, split VT sequences, plain-text
  formatting, resize reflow, immutable cells, styles, colors, and cursor state
- Interactive local PowerShell sessions with independent ConPTY read/write
  workers, bounded input buffering, resize coalescing, and prompt shutdown
- A single custom Qt Quick terminal item for cell-grid rendering, keyboard
  input, control sequences, cursor display, and IME commit text
- Automated end-to-end coverage from PowerShell through ConPTY and
  `libghostty-vt` snapshots, including input and deterministic shutdown
- Dynamic and static Qt CMake build targets
- Categorized rotating file logs with Debug-build diagnostics

### Fixed

- Preserve native maximize capability when the window is created, execute
  maximize/restore from custom non-client button messages, and expose the
  custom caption-button bounds to Windows
- Embed an explicit Windows 10/11 compatibility and Per-Monitor V2 manifest so
  Windows enables modern custom-title-bar and Snap Layout behavior
- Intercept maximize-button non-client messages before Qt so Windows receives
  the native hover gesture used by Snap Layouts
- Preserve `WS_CAPTION` and the resizable frame while removing `WS_SYSMENU` so
  DWM retains modern maximize transitions and Snap Layout integration without
  painting native caption buttons over the custom title bar
- Suppress legacy non-client theme painting that can flash over the custom
  title bar during maximize and restore transitions
- Avoid double-owning scene-graph textures when replacing terminal frames
