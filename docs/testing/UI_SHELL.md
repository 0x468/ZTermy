# Shell and navigation manual verification

This check covers the Netcatty-inspired information hierarchy implemented with
ztermy-owned QML and Windows integration. Run it on Windows 11 after changing
the title bar, navigation, terminal tabs, or theme metrics.

## Terminal workspace

1. Start the dynamic Debug build.
2. Confirm the initial local terminal opens.
3. Create two more local terminals from the title-bar `+` button.
4. Switch among all terminal tabs, then close the middle tab.
5. Narrow and widen the window.

Expected:

- Hosts, terminal tabs, and the new-tab button share the custom title bar.
- The active tab has a distinct surface and every running tab has a visible
  status indicator.
- Tabs switch and close without moving the window.
- Overflow remains clipped to the tab strip and never overlaps the native
  minimize, maximize, or close buttons.
- The terminal workspace has no vault sidebar and uses the full content width.
- A compact session bar shows status, encoding, and the search shortcut.
- The terminal keeps focus after switching or creating a tab.

## Host vault transition

1. Select `Hosts` in the custom title bar.
2. Inspect the vault navigation and host content.
3. Select any terminal tab to return.

Expected:

- The vault sidebar appears only on the Hosts workspace.
- Host content and the sidebar remain aligned without a horizontal jump or
  clipped controls.
- Returning to a terminal hides the sidebar and restores the active session.
- No terminal session is stopped while the vault is visible.

## Native title-bar behavior

1. Drag the window from the empty title-bar region to the right of the tabs.
2. Double-click that same region twice.
3. Hover and click the custom maximize/restore button.
4. Exercise minimize, restore, close, and all resize edges and corners.
5. Repeat with enough tabs to approach the available title-bar width.

Expected:

- Tabs and buttons receive clicks instead of starting a window drag.
- The empty title-bar region performs native move and
  double-click maximize/restore.
- Maximize hover exposes Windows 11 Snap Layouts.
- Resize cursors and native edge behavior remain unchanged.
- No title-bar control is covered at narrow widths.
