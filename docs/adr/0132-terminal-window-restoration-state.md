# ADR 0132: Terminal window restoration state

- Status: Accepted
- Date: 2026-09-26

## Decision

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
Those scenarios and interactive drag acceptance remain pending.
