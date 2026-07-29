# Architecture

Status: draft

## Layers

### UI

Qt Quick and QML define the application shell, navigation, panels, dialogs, and
animations. QML must not own SSH, PTY, persistence, or terminal parsing.

### Application

C++ application services coordinate commands, view models, session lifecycles,
and error presentation.

### Domain

Plain C++ models and state transitions represent hosts, workspaces, terminal
sessions, connection state, known hosts, and configuration migrations.

`TerminalEngine` is the application-owned boundary for terminal state. Its
first implementation adapts the pinned `libghostty-vt` C ABI. Ghostty handles
remain private to the adapter so upstream API changes or an engine replacement
do not spread through session or renderer code.

### Infrastructure

Adapters provide SSH, SFTP, ConPTY, persistence, credentials, logging, and
Windows shell integration.

## Terminal data flow

```text
ConPTY or SSH channel
  -> session I/O worker
  -> bounded byte queue
  -> terminal parser and screen model
  -> dirty-row snapshot
  -> GUI-thread handoff
  -> TerminalQuickItem
  -> Qt Quick scene graph
```

Input travels directly from the focused terminal item to its session writer.
It must not wait for output, persistence, logging, or a UI snapshot.

## Threading rules

- The GUI thread owns QObject/QML-facing state.
- The Qt Quick render thread owns scene graph resources.
- PTY and SSH I/O never block the GUI or render thread.
- Cross-thread messages are bounded and cancellable.
- Large output is coalesced into damage updates; it is not converted into one
  UI event per byte or character.

## Terminal rendering

The terminal viewport is a single custom `QQuickItem`. Its renderer batches
backgrounds, selections, cursor geometry, decorations, and glyphs. It retains a
glyph cache and updates only damaged rows where possible.

Formatted plain text is reserved for tests, diagnostics, and clipboard-style
operations. Rendering will consume an immutable, ztermy-owned cell snapshot;
the render thread will never access a mutable Ghostty terminal handle.

`QQuickPaintedItem` is not the target renderer. Private QRhi APIs are not used
until a measured public-scene-graph implementation proves insufficient.

## Windows window chrome

`NativeWindow`, a thin `QQuickView` subclass, owns Windows-native non-client
behavior:

- `WM_NCCALCSIZE` client-area extension
- `WM_NCHITTEST` caption, resize edges, and `HTMAXBUTTON`
- `WM_GETMINMAXINFO` work-area constraints
- DWM frame, corners, theme, border, and backdrop attributes
- system move and resize operations

The QML title bar supplies visuals and reports interactive rectangles. Native
code supplies system semantics. The resizable frame, caption metadata, and
minimize/maximize capabilities are retained so DWM continues to provide modern
window transitions and Snap Layout integration. `WS_SYSMENU` is removed to
prevent native caption buttons from being painted over the custom chrome.
`WM_NCCALCSIZE` extends the client area over the retained native frame.

The executable embeds a Windows 10/11 compatibility manifest. Besides declaring
Per-Monitor V2 DPI awareness, this opts the process into the modern Windows
behavior required by custom-title-bar Snap Layout integration.

Qt consumes some non-client input before `QWindow::nativeEvent()`. The Windows
adapter therefore installs a narrowly scoped window-procedure bridge: maximize
hit testing and hover reach `DefWindowProcW` before Qt, while unrelated messages
continue through Qt's original procedure. The bridge also consumes the legacy
non-client theme drawing messages used by `DefWindowProcW`; allowing those
messages through can briefly paint system chrome over the custom title bar
during maximize and restore transitions.

Hit-test classification is kept in a Qt-independent helper so resize, caption,
client, and maximize-button regions can be unit tested without creating a
native window.

## Logging

Qt logging categories provide subsystem-specific diagnostics. Debug builds
enable Debug-and-higher messages for `ztermy.*`; non-Debug builds suppress
Debug messages while retaining Info-and-higher application events. Logs rotate
at 4 MiB and live outside the repository under the user's local application
data directory.

## Persistence and secrets

Non-secret configuration has an explicit schema version and migration policy.
Passwords and passphrases are not serialized. Persistent secrets use a Windows
credential facility selected in a later ADR.
