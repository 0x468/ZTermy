# ADR 0018: On-demand Settings work tab

Status: accepted

## Context

NetCatty presents Settings in a separate native window with a category rail.
ztermy already owns a single native window, custom title bar, terminal work
tabs, Windows backdrop state, and focus restoration. A second native Settings
window would duplicate native-window lifecycle, DPI, material, Snap, and test
surfaces without improving the personal Windows terminal workflow.

A permanently visible Settings tab would also consume scarce title-bar space
even though settings are visited much less often than Hosts and terminal
sessions.

## Decision

Settings remains inside the main native window and is represented by one
on-demand work tab.

- A global Settings shortcut sits immediately before the native caption
  buttons.
- Activating the shortcut opens and selects the Settings tab. Repeated
  activation selects the same tab and never creates a duplicate.
- Closing the Settings tab hides it and restores the workspace that was active
  before Settings opened.
- The Settings tab participates in the existing title-bar tab strip and keeps
  its own close action.
- Settings content uses a compact category rail on the left and one
  independently scrolling category page on the right.
- Only categories backed by implemented ztermy preferences are shown. V1.1
  begins with Appearance and Terminal; unsupported Serial, SFTP, AI, and cloud
  categories are not placeholders.
- The global shortcut is a Win32 client hit-test region. The maximize caption
  region remains native so Snap Layouts continue to work.

## Consequences

- Terminal sessions remain visible and running while Settings is open.
- Settings has one lifecycle and one draft-preview surface.
- The title bar stays compact during normal terminal work.
- Keyboard automation must cover opening, singleton activation, category
  navigation, closing, and focus/workspace restoration.
- Adding a future Settings category requires real settings, persistence, and
  acceptance coverage rather than navigation-only UI.
