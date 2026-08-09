# Changelog

All notable changes will be documented in this file.

The project has not published a release.

## Unreleased

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
