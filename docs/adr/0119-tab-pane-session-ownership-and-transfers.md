# ADR 0119: Explicit ownership for terminal sessions, panes, tabs, and windows

## Status

Accepted on 2026-09-13. Implementation is authorized and tracked under V5.5.
This decision supersedes the one-session-per-tab ownership assumption in ADR
0008 and extends ADR 0050; acceptance does not imply implementation is complete.

## Context

ADR 0008 started with one session per tab. ADR 0050 added a bounded split tree
with one live session per leaf. Today `AppController::TerminalTab` owns the
local/SSH worker and session-specific sidecars, but also carries `workspaceId`
and `paneId`; `TerminalWorkspaceLayout` persists the binary tree and restore
intents. `Main.qml` detaches a pane by projecting it out of the main view and
showing another window. This is presentation-only: the persisted workspace
and owning tab do not move. These overlapping meanings make cross-tab/window
dragging, close behavior, focus, and recovery difficult to state precisely.

The owner wants pane reordering and conversion among pane, tab, and native
window, but terminal input, independent connections, and the current fast
background shutdown take priority over a rich drag animation.

## Decision

Use five distinct identities, with one-way ownership and stable IDs:

| Identity | Owns or references | Lifetime |
|---|---|---|
| `SessionId` | Exactly one local process or SSH connection, parser, snapshot, session-scoped auxiliary jobs | Until explicitly closed or process/connection ends; moving never reconnects |
| `PaneId` | Exactly one `SessionId` and one restore intent; no second live view of that session | Until its leaf is closed; moving keeps the ID |
| `WorkspaceId` | One bounded split tree, active `PaneId`, tab title/order/pin state | Until its last pane is moved or closed |
| `WindowId` | Ordered workspace tabs and native-window geometry/material | Until its last workspace is transferred or closed |
| `RestoreIntentId` | Safe local-shell or saved-profile restart description, never a live session or secret | Durable across application restarts |

Every live session appears in exactly one pane; every pane appears in exactly
one workspace; every workspace appears in exactly one window. No move may
duplicate a session, worker, input route, or connection. “Copy pane” creates a
new `SessionId` and `PaneId` from the source's saved profile/local-shell
settings; it never clones the process, SSH connection, scrollback, live input,
or secret material. The same profile in two panes still means two sessions.

An inactive tab is still a workspace. A tab can contain one or many panes.
Moving a pane to the tab strip creates a new workspace; dragging a whole tab
to a pane merges its tree as a subtree only if the resulting bounded layout
passes validation. A detached window owns its workspace rather than retaining
a hidden QML projection in the original tab.

### Structural operations

All mutations are commands over IDs, not QML objects or tab indices:

- `MovePane(source, destination, edge/center)`: move the same session and leaf;
  edge creates a split, center swaps positions without swapping session IDs.
- `ExtractPaneToTab` / `ExtractPaneToWindow`: move the same leaf and session;
  remove the empty source workspace. A one-pane tab dragged out transfers the
  entire workspace ID to the new window.
- `MergeWorkspaceIntoPane`: transfer the source tree, preserving every pane
  and session ID; reject when leaf/session limits or drop target conflict.
- `MoveWorkspaceToWindow` / `ReattachWindow`: change workspace ownership only;
  never stop a session. A closed original tab is not recreated as a phantom.
- `DuplicatePane`: start one new, independent session using a permitted restore
  intent. Failure leaves the original untouched.
- `ClosePane`, `CloseWorkspace`, `CloseWindow`: explicitly retire the affected
  sessions using the existing non-blocking shutdown path. A drag cancel or
  failed move is not a close.

For a move, preflight validates IDs, bounds, destination, source generation,
and any pending composition/confirmation state. Construct an immutable
candidate layout, then persist and publish it as one logical transaction.
No QML callback edits the model directly. A failed validation, destination
window creation, or persistence leaves the original ownership and focus
intact; an operation is idempotent by command ID to avoid double drops.

### View and input boundaries

`TerminalItem` is a view of a session snapshot, not session ownership. An
in-window move may rebind a view. A cross-window move should create/bind a
destination view and retire the source view; it must not assume moving a live
Qt Quick item between scenes preserves render resources. Keep the same
backend session and cached snapshot throughout. Route input by `SessionId`
plus a current view/activation generation so an old view cannot send keys
after transfer. Delay or cancel a drag while IME composition or a modal
host-key/credential decision is active; do not silently lose or reroute text.
On successful transfer, focus the moved pane and update the window/tab title;
on cancel, restore the previous focus and selection without sending input.

### Persistence and recovery

`workspace_state.json` is currently schema 7. Implementation would require
a new monotonic schema version, a schema-7 fixture, forward-version rejection,
atomic write, and migration tests. Persist only layout/window placement and
safe restore intents, not live `SessionId`, terminal content, credentials, or
secret-bearing commands. After restart, local intents may start according to
existing restore policy; SSH remains disconnected until explicit reconnect.
If a window or pane cannot be restored, quarantine only the invalid workspace
and offer a safe fallback tab instead of repeatedly crashing startup.

## Alternatives considered

1. Extend the current QML-only detach projection. Smallest immediate diff,
   but source tab ownership and close/recovery semantics remain ambiguous;
   inadequate for cross-tab transfers.
2. Reparent a live `TerminalItem` across `QQuickWindow`s. Attractive visually,
   but couples a live session to render-scene lifetime and raises material,
   DPI, focus, and teardown risks. Reject until a measured prototype proves
   it safer than rebinding from the cached snapshot.
3. Give two panes the same session. Reject: input ownership, resize, copy mode,
   and selection would become ambiguous and violate connection independence.

## Boundaries and owner decisions

- No external Agent/harness integration and no third-party icon/code copying.
- No new Shell input interception; completion and editing remain with Shells.
- A single-pane detached window reattaches to its original tab if that tab
  still exists; otherwise it becomes a new tab. If the original tab is full,
  use a new tab without discarding or closing sessions.
- Whole multi-pane tab merging is included in the first implementation stage.
- Confirm window closure only when it will end active sessions. Moving,
  reattaching, or closing a window containing only ended sessions needs no
  confirmation.

## Implementation gates after approval

1. Extract a session registry and ID-based ownership API without changing UI.
2. Add pure domain transfer transactions and schema migration with rollback,
   bounds, restore, and failure tests.
3. Add in-window gestures, then tab extraction/merge, then native-window
   transfers; verify each checkpoint before the next.
4. Run focused tests during iteration and full Debug/static, clang-tidy,
   migration, DPI/material, IME, native-window/taskbar, and real SSH checks at
   the milestone. Preserve a downgrade/backup path for schema changes.

## Evidence and references

### Implementation checkpoint (2026-09-13, in progress)

Later owner decision on 2026-09-13 supersedes close confirmation and whole-tab
drag gestures: explicit tab/window close and tray Exit no longer require a second
confirmation. Tab-title dragging only reorders tabs; pane-title dragging owns
insertion, layout transfer and detachment. Embedded pane drags are captured by
the stable main-window surface so hovering another tab cannot destroy the drag's
source identity. Default product accent colors now use purple variants; custom
and system accent choices remain unchanged.

Owner feedback amendment, later on 2026-09-13: new detached windows are temporarily
single-pane surfaces, with hidden-by-default pane headers and custom frameless
window controls. They cannot receive additional panes; whole-tree merges between
main-window tabs remain in scope. Dropping between tabs inserts a new tab at that
position; dropping on a tab or pane merges there. The restore button retains the
original-tab fallback policy. See [feedback and manual checklist](../testing/V5_TAB_DRAG_OWNER_FEEDBACK.md).
These latest changes are compile-only by owner request, not runtime-accepted.

- `TerminalSessionState` now holds the existing independently owned backend and
  sidecars. `AppController` retains its `TerminalTab` alias during the transition;
  layout moves preserve the same owning `unique_ptr` and session IDs.
- `TerminalWorkspaceTransfer` operates on a candidate `WorkspaceState`, covering
  same/cross-workspace leaf movement, stable-ID swaps, extraction and subtree
  merge. Controller persistence succeeds before publishing new membership.
- Workspace schema 8 records `windowId` and `returnWorkspaceId`. Schema 7 defaults
  to the main window; restore intent, quarantine and unrelated fields survive.
- `TerminalWindowCoordinator` creates independent Qt windows for detached
  workspaces. It no longer removes a pane only from a QML projection. Window
  geometry persistence and the full cross-window gesture matrix remain open
  verification/implementation items; do not infer completion from this checkpoint.
- View signal connections use a QObject context destroyed on rebinding, so Qt
  drops queued deliveries to retired bindings. Transfers reject ongoing IME
  preedit and pending host-key decisions instead of moving their input target.
- The current in-process UI marks each drop as completed before scheduling it.
  A general command-ID/revision protocol is not yet implemented; duplicate or
  stale gesture coverage remains an explicit gate.

- Current ownership: `src/application/AppController.h` (`TerminalTab`),
  `src/domain/workbench/WorkspaceState.h` (`TerminalWorkspaceLayout`),
  `src/ui/qml/Main.qml` (`detachTerminalPane`).
- [Qt 6.8 DragHandler](https://doc.qt.io/qt-6.8/qml-qtquick-draghandler.html),
  [Drag](https://doc.qt.io/qt-6.8/qml-qtquick-drag.html),
  [DropArea](https://doc.qt.io/qt-6.8/qml-qtquick-droparea.html), and
  [Window](https://doc.qt.io/qt-6.8/qml-qtquick-window.html) describe the
  gesture, drop, and separate-scene primitives. The recommendation to rebind
  a viewport rather than reparent it is our risk inference, not a Qt mandate.
