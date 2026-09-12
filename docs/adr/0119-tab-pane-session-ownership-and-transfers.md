# ADR 0119: Explicit ownership for terminal sessions, panes, tabs, and windows

## Status

Proposed — design review only. No implementation, schema change, or replacement
of ADR 0008 / ADR 0050 is authorized until the owner approves this proposal.

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

## Proposed decision

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

## Boundaries and approval questions

- No external Agent/harness integration and no third-party icon/code copying.
- No new Shell input interception; completion and editing remain with Shells.
- No implementation of cross-tab/window ownership before explicit approval.
- Approval must settle the user-facing choices in the companion interaction
  proposal: where a one-pane window reattaches, whether whole-tab merge is
  enabled initially, and what confirmation closes a window containing active
  sessions.

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

- Current ownership: `src/application/AppController.h` (`TerminalTab`),
  `src/domain/workbench/WorkspaceState.h` (`TerminalWorkspaceLayout`),
  `src/ui/qml/Main.qml` (`detachTerminalPane`).
- [Qt 6.8 DragHandler](https://doc.qt.io/qt-6.8/qml-qtquick-draghandler.html),
  [Drag](https://doc.qt.io/qt-6.8/qml-qtquick-drag.html),
  [DropArea](https://doc.qt.io/qt-6.8/qml-qtquick-droparea.html), and
  [Window](https://doc.qt.io/qt-6.8/qml-qtquick-window.html) describe the
  gesture, drop, and separate-scene primitives. The recommendation to rebind
  a viewport rather than reparent it is our risk inference, not a Qt mandate.
