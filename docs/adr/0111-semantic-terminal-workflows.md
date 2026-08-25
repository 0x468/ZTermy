# ADR 0111: Build semantic terminal workflows on stable grid identities

- Status: Accepted
- Date: 2026-08-26
- Milestone: 0.4.3

## Context

ztermy 0.4.1 made selection stable across scrollback and resize by keeping
libghostty tracked grid references on the session worker. Version 0.4.2 then
moved VT key, mouse, and focus encoding to that worker so the live terminal
state remains the single protocol authority. Links, Quick Select, keyboard
Copy Mode, OSC 52, drag/drop, and tab recovery must preserve those contracts.

There are three distinct kinds of state involved:

1. terminal-owned state, such as OSC 8 hyperlinks, wrapped logical rows,
   scrollback positions, selections, and OSC 52 effects;
2. derived viewport state, such as detected URLs and Quick Select labels; and
3. application workflow state, such as SFTP availability, a reconnectable SSH
   profile, or a recently closed tab description.

Putting any of these into a QML object per cell would multiply allocations,
make render cost depend on scrollback size, and create a second coordinate
authority. Conversely, treating every target as a local URL would be incorrect
for paths emitted by a remote shell. Reusing a dead PTY or SSH object for tab
recovery would also misrepresent process lifetime.

The behavior was checked against the OSC 8 proposal, xterm OSC 52 semantics,
the public API of the pinned libghostty revision, Qt URL/MIME/drop APIs, and the
documented interaction models of Windows Terminal, WezTerm, and kitty.

## Decision

### Semantic snapshot data

1. A terminal snapshot carries compact row metadata and link spans alongside
   cells. Cells do not own URI strings.
2. OSC 8 targets are stored in a snapshot-local intern table. A link span
   refers to an interned identifier and a logical grid range. Equal URIs are
   stored once per snapshot, and spans may cross wrapped display rows without
   duplicating the URI.
3. Auto detection operates on bounded, changed logical lines off the GUI and
   render threads. It emits typed spans for HTTP/HTTPS URLs, email addresses,
   Windows and POSIX paths, `path:line:column`, IP addresses, `host:port`, and
   Git hashes. It never runs from `updatePaintNode` or creates QML cell
   objects.
4. An explicit OSC 8 span claims its cells and always wins over an overlapping
   detected span. Detection cannot change the target supplied by the terminal.
5. Remote paths retain a remote-path type and session identity. They can be
   copied, inserted, or located through that session's SFTP capability, but
   they are never passed to the local desktop as though they were local files.

### Pointer ownership and link activation

Pointer routing is evaluated for every effective modifier transition and uses
this order:

1. Shift plus a selection gesture always belongs to local selection, even when
   a TUI requested mouse reporting.
2. Ctrl plus primary-button click on a link is a local link gesture.
3. Otherwise, an active TUI mouse mode owns the pointer event.
4. Otherwise, ztermy applies its local selection and context-menu behavior.

Hover underlines a target. Ctrl changes the pointer to an activation cursor and
shows the resolved target in the themed tooltip. Activation occurs on release,
only if press and release still identify the same span and movement remained
below the platform drag threshold. Pressing or releasing Ctrl while stationary
must refresh hover presentation. These rules prevent an attempted selection or
drag from opening a link and preserve Shift as the reliable local-selection
escape hatch.

### Quick Select

Quick Select is a transient `TerminalItem` mode, defaulting to
Ctrl+Shift+Space. It considers semantic spans in the current visible viewport
only. Prefix-free one- or two-character labels are assigned in deterministic
visual order. The terminal custom item paints the dimming and labels in its
existing scene-graph subtree; QML does not instantiate a delegate for every
match.

Typing a label copies its value and exits. Shift plus the label inserts the
value at the prompt; Ctrl plus the label, or Enter on the active match, opens a
target only when that target type has an open capability. Escape cancels. A
viewport or snapshot generation change recomputes the bounded label set rather
than retaining stale cell coordinates.

### Keyboard Copy Mode

Copy Mode is another terminal-item mode, entered with Ctrl+Shift+X. Its anchor,
active endpoint, and granularity are worker-owned libghostty tracked grid
references, not viewport row indexes. Navigation covers cells, words, logical
lines, pages, top/bottom, rectangular selection, and active-endpoint switching.
The initial key map follows familiar terminal conventions: arrows/Home/End/Page
navigate, Shift extends, Ctrl+arrow moves by word, `v`/`V`/Ctrl+V select
cell/line/rectangle granularity, `o` switches endpoint, `y` or Enter copies and
exits, and Escape cancels.

Commands that operate on prompts or outputs do not infer shell semantics from
painted text. They reuse `SemanticTerminalObserver` and `CommandBlockStore` and
must preserve each block's rich/basic/none capability and
complete/partial/truncated/interleaved evidence. Unavailable precision disables
the corresponding action rather than silently presenting an approximation as
exact. In 0.4.3 this contract exposes bounded actions for copying the latest
command and its complete retained output. Previous/next prompt navigation and
semantic block selection require a stable stream-offset-to-tracked-grid mapping
and are intentionally deferred instead of guessing from viewport coordinates.

### OSC 52 clipboard write

Only OSC 52 write-to-system-clipboard is advertised. Clipboard reads remain
unsupported because the pinned public callback treats them as ignored.

The libghostty terminal effect callback is synchronous. It validates a text
MIME payload, copies at most 8 MiB into bounded engine-owned pending state, and
returns without blocking, calling Qt, or re-entering the terminal. Repeated
pending writes coalesce to the latest value. The local or SSH session worker
drains the effect only after releasing the engine mutex, then queues the final
clipboard mutation to the GUI thread. Engine/session destruction clears pending
effects so no callback targets a dead QObject.

### Drag and drop

- Dropped text follows the normal paste path and its existing multiline and
  bracketed-paste behavior.
- Files dropped on a local terminal are shell-quoted for the active shell and
  inserted without implicit execution.
- Files dropped on an SSH terminal are uploaded through that tab's SFTP
  capability. The destination is the known remote working directory, then the
  active SFTP directory, then the remote home directory. If none is known, the
  UI asks for a destination rather than inventing one. Successful upload does
  not execute the file.
- Virtual-file MIME providers are deferred; unsupported drops remain
  non-destructive and visible to the user.

### Tabs and action capabilities

Duplicate and reopen always create a fresh session from a declarative
description:

- a local duplicate uses the same terminal profile and last known working
  directory;
- an SSH duplicate or reopen uses the saved SSH profile;
- reopen-last-closed keeps at most ten lightweight descriptions and never
  retains a terminal engine, ConPTY handle, SSH connection, credential, or
  scrollback snapshot;
- reconnect replaces the disconnected session behind the same visible tab
  identity and preserves tab-local UI state where valid.

Close-other, close-right, reorder, and custom-title actions mutate the workspace
layout but do not move a live session between ownership domains. Restored or
reopened tabs state clearly that a new process/connection is being created.

The application controller exposes capability facts rather than
presentation-derived assumptions, including whether the active tab is a
terminal, connected/reconnectable, local/SSH, backed by a saved profile, able
to use SFTP, holding a selection, hovering a typed link, in Quick Select or Copy
Mode, and offering exact or approximate command blocks. Registered actions and
QML menus consume those facts; QML does not inspect terminal internals to invent
its own semantic state.

## Performance and lifetime constraints

- Link URI storage is interned and spans are proportional to matches, not
  terminal cells or scrollback size.
- Auto detection and label generation are bounded to changed logical lines or
  the visible viewport. Regex work, URL parsing, shell quoting, SFTP dispatch,
  and clipboard effect handling never occur in scene-graph painting.
- Quick Select labels are rendered in the terminal node tree and allocate no
  QML object per label or cell.
- Worker-produced snapshots and effects use generation/session identifiers.
  Late results from a resized, replaced, disconnected, or destroyed session are
  discarded.
- OSC 52 payloads and the closed-tab stack have explicit bounds. High-frequency
  snapshots, hover transitions, and clipboard writes coalesce.
- No GUI-thread operation waits for terminal parsing, PTY/SSH I/O, URL
  detection, or SFTP transfer.
- Modal/transient modes are cancelled when their owning tab, snapshot, popup,
  or session becomes invalid. Tracked selection references are released on the
  worker that owns the engine.

## Consequences

- Hyperlinks, selection, and command blocks retain distinct provenance and
  precision instead of being flattened into display text.
- Expert workflows remain direct: Ctrl+click opens, Shift always selects, Quick
  Select defaults to copy, file drops insert or upload without implicit
  execution, and reconnect is available in place.
- The snapshot contract becomes richer, but avoids per-cell heap strings and
  keeps QML presentation-only.
- Reopened and duplicated tabs deliberately do not preserve process identity or
  scrollback; the UI can make that truthful and predictable.

## Rejected alternatives

- **Store a URI string on every terminal cell:** excessive copying and memory
  growth for long links and large viewports.
- **Detect links during painting:** unpredictable render-thread latency and
  duplicate work every frame.
- **Let Ctrl+click bypass Shift selection or TUI ownership ambiguously:** makes
  selection unreliable and can send part of a local gesture remotely.
- **Represent labels as QML delegates:** object churn scales with match count
  and conflicts with the one-custom-item terminal architecture.
- **Implement Copy Mode with visible row indexes:** anchors drift when output,
  reflow, or scrollback eviction changes viewport coordinates.
- **Call `QClipboard` inside the terminal effect callback:** violates threading
  and re-entrancy constraints.
- **Insert local paths into an SSH shell:** the remote machine cannot resolve
  them and the action appears to work while being meaningless.
- **Retain live sessions for reopen:** leaks resources and falsely implies a
  closed remote process survived.

## Verification

- Domain tests cover overlap precedence, wrapped links, typed detector output,
  intern-table deduplication, label assignment, shell quoting, and bounded
  closed-tab descriptions.
- Engine/session tests cover OSC 8 extraction, tracked Copy Mode navigation,
  OSC 52 write/oversize/coalescing/lifetime, and stale-generation rejection.
- UI tests cover modifier precedence, release-only link activation, Quick
  Select and Copy Mode cancellation, action capability enablement, and drop
  routing.
- Runtime acceptance covers light/dark themes, local PowerShell, SSH, a
  mouse-aware TUI, remote SFTP, disconnect/reconnect, and static Release
  packaging.

## References

- OSC 8 hyperlinks:
  <https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda>
- xterm control sequences:
  <https://invisible-island.net/xterm/ctlseqs/ctlseqs.html>
- Windows Terminal interaction:
  <https://learn.microsoft.com/windows/terminal/customize-settings/interaction>
- Windows Terminal selection:
  <https://learn.microsoft.com/windows/terminal/selection>
- Windows Terminal duplicate tab:
  <https://learn.microsoft.com/windows/terminal/tutorials/new-tab-same-directory>
- WezTerm hyperlink rules:
  <https://wezterm.org/config/lua/config/hyperlink_rules.html>
- WezTerm Quick Select: <https://wezterm.org/quickselect.html>
- WezTerm Copy Mode: <https://wezterm.org/copymode.html>
- kitty hints: <https://sw.kovidgoyal.net/kitty/kittens/hints/>
- Qt desktop services: <https://doc.qt.io/qt-6/qdesktopservices.html>
- Qt MIME data: <https://doc.qt.io/qt-6/qmimedata.html>
- Qt Quick custom items: <https://doc.qt.io/qt-6/qquickitem.html>
