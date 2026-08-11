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
  -> terminal parser and screen model
  -> immutable cell snapshot
  -> GUI-thread handoff
  -> TerminalQuickItem
  -> Qt Quick scene graph
```

Input travels directly from the focused terminal item to its session writer.
It must not wait for output, persistence, logging, or a UI snapshot.

## Terminal workbench data flow

```text
terminal toolbar or workbench page
  -> AppController named action
  -> active terminal-tab state
  -> ShellHistoryProvider or QuickCommandStore
  -> immutable QML-facing view model

Run or Insert
  -> explicit user action
  -> active session input queue
  -> terminal paste encoding or exact command + carriage return
```

Remote history uses an auxiliary exec channel on the active authenticated SSH
session. The session worker alone touches libssh2. It pumps auxiliary-channel
state between bounded terminal-I/O iterations so history refresh cannot block
interactive input. Parsed remote history is per-session memory, never automatic
persistent history. Commands captured reliably from open terminal tabs are
aggregated into an in-memory global view and disappear with those tabs. Code
snippets are a separate versioned global store and contain only text the user
explicitly saves in the Scripts surface.

## Threading rules

- The GUI thread owns QObject/QML-facing state.
- The Qt Quick render thread owns scene graph resources.
- PTY and SSH I/O never block the GUI or render thread.
- Cross-thread messages are bounded and cancellable.
- Large output is coalesced into damage updates; it is not converted into one
  UI event per byte or character.
- Local terminal output is read on a dedicated `std::jthread`. Input uses a
  separate writer with a bounded 1 MiB queue, and resizes are coalesced to the
  latest geometry.
- Snapshot delivery keeps only the latest immutable frame while a GUI-thread
  delivery is pending. Session shutdown cancels blocking reads and joins both
  workers before releasing ConPTY state.

## Terminal rendering

The terminal viewport is a single custom `QQuickItem`. The first renderer
paints one CPU image per immutable snapshot and uploads it as one public scene
graph texture. This establishes correct ownership and thread boundaries
without creating a QML object per cell.

The target renderer batches backgrounds, selections, cursor geometry,
decorations, and glyphs, retains a glyph cache, and updates only damaged rows.
The current full-frame texture path is intentionally replaceable after
profiling establishes the required batching and damage strategy.

Formatted plain text is reserved for tests, diagnostics, and clipboard-style
operations. Rendering consumes an immutable, ztermy-owned cell snapshot; the
render thread never accesses a mutable Ghostty terminal handle.

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
Passwords and passphrases never enter profile JSON. Installed mode stores them
as generic entries in Windows Credential Manager. Portable/custom-data modes
use a versioned AES-256-GCM vault whose key is derived from a user-supplied
master password with scrypt; session-only storage remains available. A global
coordinator performs copy, read-back verification, active-store switching, and
optional source cleanup. See ADR 0021.

## V3 AI extension

The approved V3 direction adds a provider-independent AI layer without changing
terminal ownership:

```text
TerminalOutputFanout + shell lifecycle + immutable terminal snapshots
  -> CommandBlockStore / TerminalFrameSource
  -> AiContextBroker (provenance, redaction, bounds, preview)
  -> AiOrchestrator
  -> AiProvider adapter over asynchronous Qt Network

model tool proposal
  -> schema and scope validation
  -> AiPermissionPolicy
  -> AiToolExecutionBroker
  -> existing terminal / SSH / SFTP / telemetry / script / note service
```

Provider payloads, prompts, permissions, and tools remain C++ concerns. QML owns
the AI panel and lightweight interaction only. Ordered semantic capture retains
terminal output or marks an explicit coverage gap; derived frame/UI/provider
observation may coalesce stale updates. Neither can backpressure terminal I/O.
Provider keys reuse the installed/portable credential boundary; large transcript
data never enters Windows Credential Manager.

The detailed target design is in `AI_ARCHITECTURE.md`, the product milestones in
`V3_AI_PROGRAM.md`, the top-level decision in ADR 0054, shell activation in ADR
0055, and replay-safe agent execution/session ownership in ADR 0056.
