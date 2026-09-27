# ADR 0132: Terminal window restoration state

- Status: Accepted
- Date: 2026-09-26

## Decision

### Window movement and layout editing (2026-09-27)

Detached windows start with their Tab strip hidden. With the strip hidden, the
Pane handle moves the whole native window and never implicitly reattaches it;
clicking reveals the strip. With the strip visible, the handle edits Pane layout
and clicking hides the strip. In the main window it is exclusively a Pane drag
handle. Separate Pane captions are not part of this interaction model.

Dragging blank caption space only moves the window. Dragging a specific Tab can
reorder, transfer or merge that Tab. An explicit button beside the detached
window's new-terminal button returns all its Tabs to the main window in order,
preserving Pane layouts and the selected Tab.

An incoming layout drag over a hidden strip's top hot zone temporarily reveals
the strip. Cancellation or a content drop hides the preview; a successful Tab
insertion keeps it visible. Content drops continue to target individual Panes.
Drag previews use an input-transparent, non-activating native window so they
remain visible outside the source window. Automatic splits choose the longer
viewport axis; explicitly requested horizontal/vertical splits remain unchanged.

This is an interaction contract, not a persisted-schema change. Title-bar
visibility starts hidden on each newly created/restored detached window.

The first interaction pass has focused native runtime evidence for hidden-strip
startup/toggling without selection changes, cross-window Tab dragging and
cancellation, exact non-active Pane drops, geometry-adaptive splitting, and
explicit all-Tab reattachment with unchanged Pane trees and source-window
closure. Terminal titles retain the existing policy: manual names take priority,
then a terminal-reported title when enabled, then the Profile/default name.
PowerShell changing its startup Profile label to an executable path is consistent
with this terminal/console title path; no executable-name filtering is introduced.
Physical mixed-DPI verification remains unavailable.

The detached strip now shares the main window's new-terminal menu and saved-host
credential flow. New local, specific Shell and saved-host entries are available;
main-only host management and closed-Tab recovery stay in the main menu. The plus
follows the Tabs, immediately followed by reattach-all; remaining blank space is
reserved for native window movement before the three caption buttons.

Hidden caption controls also disable native maximize hit testing and discard
stale hover/press state. When visible, native maximize/Snap hit testing remains.
Incoming layout drags preview and merge into the Tab under its center; Tab edges
insert a separate Tab. Focused runtime checks cover menu ownership, local
credential prompts, inactive-Tab selection/merge with the other Tab preserved,
hidden native hit testing and visible maximize/restore. The latest Tab merge
check exercises actual QML geometry and controller transfer without desktop
pointer injection; its automated native drag rerun was blocked by foreground-window
capture. The owner subsequently confirmed real-pointer hover over an inactive
detached Tab and merge acceptance on 2026-09-27, closing that manual check.

The obsolete `TerminalPaneHeader.qml` component and its resource registration,
main-window header-state map, recursive header properties, native Pane-caption
hover/press plumbing and header-only drag fallback are removed. Detached Tab-bar
toggling is named `toggleTabBar`; Pane geometry no longer has dead title-height
branches. Focus/drag and scrollbar checks exercise the current handle/toolbar
contract rather than the removed Pane-title toggle.

Cleanup validation passes with the dynamic Release build: six scrollbar cases
(single/horizontal/vertical layouts, toolbar hidden/shown), full-height Pane
geometry, handle focus/reordering/detach, per-workspace zoom, explicit reattach,
detached caption/menu checks and whole-Tab merge. The old zoom test's C++ invocation
now supplies the workspace argument explicitly; QML default parameters do not
provide a one-argument Qt meta-method overload. The window-movement auto-docking
test was replaced with the supported explicit-reattach action. Evidence includes
`pane-transfer-cleanup-575fc61932ca415c800bf47bb6d7e105`,
`detached-chrome-cleanup-01afbca6c7b247ba9e5ceeea1f6a7da7`, and
`tab-merge-cleanup-aac3a9dd7d8c4caf89b570f9fffd5c0f` under the dynamic Release
`test-data` directory. These use isolated settings and Qt event injection;
the owner's real-pointer acceptance remains separately attributed above.

Workspace schema 10 adds a `terminalWindows` collection, separate from Tab/Pane
topology. Each record identifies its window (`main` or an existing detached
owner ID), selected workspace, preferred screen name, normal client geometry
in Qt logical pixels, and a maximized flag. Normal geometry must not be replaced
by minimized or maximized bounds. Minimized is not a persisted startup state.

The domain model contains no Qt UI types. Screen discovery, normal-placement
capture and applying native state belong to platform/UI integration. Restoring
must clamp to an available screen when a monitor disappears; stored screen names
are hints, not a prerequisite for restoring a session.

Window selection is advisory: a closed or moved Tab can leave a stale ID until
the next placement save. Readers must select a surviving Tab owned by that
window instead of rejecting otherwise valid session topology. A stale window
record does not create an empty detached window; topology determines live owners.

Window records are bounded to the existing maximum Tab count plus the main
window. IDs are unique and bounded; coordinates allow negative monitor origins
but are limited to +/-1,000,000 logical pixels. Dimensions are integral and
positive, at most 32,768 pixels. These are serialization sanity limits, not
screen-clamping rules or UI minimum sizes.

Schema 9 documents load with no window records and keep all existing state.
Subsequent writes use schema 10. Unsupported future versions stay read-only.
Normal store atomic writes, backup recovery and unchanged-payload suppression
remain in effect. Geometry does not contain terminal contents or credentials.

The existing session-layout restore switch controls the complete layout;
detached restoration additionally requires its own switch. Connection startup
continues to obey local reopen and remote reconnect settings. Placement changes
must be coalesced rather than synchronously written for every mouse-move event.

## Implementation status

The domain and persistence contract and schema-9 migration are implemented and
tested. Application settings schema 40 adds `restoreDetachedWindows` (default
true), under the existing session-layout master switch. Disabling only detached
restoration keeps every restored Tab/Pane but moves ownership to the main window
and discards detached placement records. This policy runs only at startup; editing
the preference does not rearrange or restart live sessions. The settings control
is disabled with the master switch while retaining its preference.

Window placement capture/application is wired through WindowControl and the
window coordinator. A 200 ms coalescer updates controller memory, not the store
on every move; existing layout saves and a synchronous shutdown snapshot flush
persist the latest placement. Restoration seeds normal geometry while hidden,
clamps to the selected available screen and never restores minimization.
Focused controller checks verify that 100 placement updates leave the store
unchanged until shutdown and that the final snapshot is persisted. Offscreen
window tests cover bounds and hidden/maximized transitions.

`scripts/verify_window_restore.ps1` additionally seeds isolated three-Tab/two-window
state and launches the actual application twice, reusing the first shutdown's
saved document for the second launch. Both runs check main-window logical bounds
and selection, detached native maximization and selection, preserved normal bounds
and topology, and no remaining child processes. This passed with the software
backend at normal and forced 125% scale. The in-process smoke uses the existing
application shutdown path; closing a single window is deliberately not used as
an equivalent to exiting the whole application. The `-RemovedScreen` variant uses
a nonexistent monitor name and (-90000, -90000) saved origins. At normal and 125%
scale, real windows restored inside the current work area; unmaximizing the
detached window preserved its 800x520 normal size. This covers startup after a
screen is absent, not live display unplugging or physical mixed-DPI movement.
Those physical multi-display scenarios remain pending; single-display drag
acceptance is recorded in the interaction closeout above.
