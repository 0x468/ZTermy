# ADR 0135: Immersive title bar and single-workspace detached windows

- Status: Accepted
- Date: 2026-10-01
- Supersedes: ADR 0132's multi-Tab detached-window interaction, not its persistence and native placement guarantees.

## Decision

The main window supports a persistent title bar and an opt-in auto-hide mode.
Auto-hide reserves a separate eight-logical-pixel strip above the terminal.
Only that strip reveals the title bar; terminal hover does not. The top two
logical pixels retain native resize hit testing, leaving the remainder usable
for native window dragging. The expanded title bar overlays content instead
of changing its geometry or terminal grid.

Hover reveals after 150 ms. Leaving starts a 300 ms dismissal delay. Switching
Tabs never resets hover/reveal state. Pointer presence anywhere in the revealed
bar, an open title-bar menu, active dragging, or keyboard-visible title-bar
focus keeps it open. Hidden controls have no native maximize/Snap hit target.
The overlay uses an opaque themed floating surface so terminal text cannot
bleed through at zero material opacity. Persistent terminal chrome and selected
Tabs do not stack another opaque background over the workspace material.
Reveal/dismiss use shared Motion enter/exit fades without shifting native hit
coordinates. The entire painted bar accepts pointer and wheel input, including
gaps between controls and the fade-out interval; background gaps drag the
window rather than activating the covered terminal or form.

Tab widths support equal-width titles that shrink together (default) and the
previous active-title/other-icons policy. Equal widths range from 184 to 38
logical pixels, hide labels below 76 pixels, and retain scrolling and the Tab
list when even icons do not fit. Selection is marked by themed ink and a small
accent indicator rather than a bright opaque pill.

A detached window owns one workspace/Tab with multiple Panes. It has no Tab
strip or Tab insertion targets. New/duplicate actions create Panes. Incoming
layouts merge into a target Pane. Window controls are an optional overlay;
their handle always moves the whole window, without implicit reattachment.
Multi-Pane detached windows have a separate layout-drag handle. Explicit
reattach preserves the entire Pane layout as one main-window Tab.
The Pane toolbar's transfer action is context-sensitive: detach in Main,
return the entire workspace in a detached window. Both window types paint one
identical theme tint in the QML scene above the native material, not a tint in
the native window clear colour that appearance configuration can overwrite.

Legacy multi-Tab window records split into separate windows without dropping
layouts, session identities or manual titles. The previously selected workspace
retains the original window placement; the others copy its geometry with a
small offset and still pass through existing screen-visibility correction.

The application settings schema advances monotonically from 40 to 41 for
`autoHideTitleBar` and `tabWidthMode`. Older documents default missing fields;
current documents reject malformed values. The workspace document structure
does not change.

## Verification

Tests target the actual risks: old-document preservation and malformed settings,
legacy workspace ownership/placement preservation, native drag/resize regions,
pointer-held switching across live Tab delegates, terminal geometry/grid
stability, theme captures, detached Pane creation/merge and explicit return.
Native desktop checks and screenshot review remain required; compilation alone
does not establish interaction correctness.
