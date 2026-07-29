# Shell and navigation manual verification

This check covers the Netcatty-inspired information hierarchy implemented with
ztermy-owned QML and Windows integration. Run it on Windows 11 after changing
the title bar, navigation, terminal tabs, or theme metrics.

## Terminal workspace

1. Start the dynamic Debug build.
2. Confirm the initial local terminal opens.
3. Create two more local terminals from the title-bar `+` button.
4. Switch among all terminal tabs, then close the middle tab.
5. Close every remaining tab, inspect the empty workspace, then open a new
   local terminal from its primary action.
6. Close every tab again and use **Browse hosts**.
7. Narrow and widen the window.

Expected:

- Hosts, terminal tabs, and the new-tab button share the custom title bar.
- The active tab has a distinct surface and every running tab has a visible
  status indicator.
- Terminal tab widths remain bounded between 126 and 190 px; extracting the
  shared tab action does not change title-bar spacing or hit-test boundaries.
- Hosts, new-tab, tab-close, and search actions use one crisp stroke-icon
  family instead of font symbols; icons remain centered at all tested scales.
- Tabs switch and close without moving the window.
- Overflow remains clipped to the tab strip and never overlaps the native
  minimize, maximize, or close buttons.
- The terminal workspace has no vault sidebar and uses the full content width.
- A compact session bar shows status, encoding, and the search shortcut.
- The terminal keeps focus after switching or creating a tab.
- Closing the final tab presents a centered, keyboard-reachable empty state
  with **New terminal** and **Browse hosts** recovery actions.
- **New terminal** creates exactly one local session and restores terminal
  focus; **Browse hosts** changes workspace without creating a session.

## Host vault transition

1. Select `Hosts` in the custom title bar.
2. Inspect the vault navigation and host content.
3. Select any terminal tab to return.

Expected:

- The vault sidebar appears only on the Hosts workspace.
- Hosts and Settings use the same 38 px navigation geometry, accent marker,
  hover surface, and keyboard-focus border.
- Host content and the sidebar remain aligned without a horizontal jump or
  clipped controls.
- Returning to a terminal hides the sidebar and restores the active session.
- No terminal session is stopped while the vault is visible.

## Keyboard navigation

1. Open Hosts, then press Tab without using the mouse.
2. Move through the Hosts and Settings navigation actions, the local-machine
   action, title-bar tabs, tab close buttons, and the new-tab action.
3. Activate each action once with Space and once with Enter.
4. Continue tabbing into the active page controls.

Expected:

- Each self-drawn action participates in the tab order.
- Keyboard focus is always visible with the shared focus color.
- Screen readers receive a button role and a specific accessible name.
- Hosts and Settings switch without creating or stopping sessions.
- The local-machine and new-tab actions each create exactly one terminal.
- Keyboard activation never begins a native window drag.

## Shared action buttons

1. In Hosts, hover and keyboard-focus **New host**, **Connect**, **Edit**,
   **Copy**, and **Delete**.
2. Open the credential, host-key, delete, multiline-paste, and Settings action
   rows as they become available.
3. Inspect ordinary, green primary, red destructive, and disabled buttons in
   Dark and Light themes.
4. Activate each available action with mouse, Space, and Enter; cancel
   destructive or secret-bearing operations unless that operation is the
   intended test.

Expected:

- Ordinary, primary, destructive, hover, pressed, focused, and disabled states
  have stable geometry and readable text.
- The focus border remains visible over green and red surfaces.
- Disabled Connect cannot be activated and uses the disabled pointer.
- Mouse and keyboard activation invoke each action exactly once.
- Buttons do not retain dark-only colors in Light theme.

## Shared editable fields

1. Exercise Host search, terminal search, profile name, group, host, port,
   username, private-key path, Settings font family, password, and passphrase
   fields.
2. Type, select with mouse and keyboard, move the caret, and use a Windows IME
   in non-secret fields.
3. Verify the port validator rejects values outside 1 through 65535.
4. Verify password and passphrase fields remain masked while editing.
5. Repeat focus, hover, selection, disabled, Dark, and Light states.

Expected:

- Standard fields share stable height, padding, border, selection, and visible
  focus; compact terminal search remains 30 px high.
- Editable fields use the I-beam pointer and preserve Qt text selection, IME,
  validator, Escape, and Enter behavior.
- Password and passphrase text is masked and never appears in status text or
  logs.
- Theme, hover, focus, and validation changes do not move neighboring
  controls.

## Confirmation dialogs

1. Open the saved-host delete confirmation with the mouse, cancel with Escape,
   and open it again with the keyboard.
2. Copy two harmless lines, request a terminal paste, cancel with Escape, then
   repeat and confirm the paste.
3. Repeat both dialogs near the minimum supported window size.

Expected:

- Cancel receives initial keyboard focus; Enter cannot accidentally select the
  destructive or paste action when the dialog first opens.
- Escape rejects the action, sends no terminal bytes, and restores focus to
  the invoking control.
- Confirming delete restores focus to the New host action after the deleted
  card disappears.
- Dialogs remain at least 24 px from the window edge, wrap required text, and
  use readable Dark and Light theme colors.

## Native title-bar behavior

1. Drag the window from the empty title-bar region to the right of the tabs.
2. Double-click that same region twice.
3. Hover and click the custom maximize/restore button.
4. Exercise minimize, restore, close, and all resize edges and corners.
5. Repeat with enough tabs to approach the available title-bar width.
6. Repeat at 100%, 125%, 150%, and 200% display scaling when available, and
   inspect the Hosts, new-tab, tab-close, and search icons.

Expected:

- Tabs and buttons receive clicks instead of starting a window drag.
- The empty title-bar region performs native move and
  double-click maximize/restore.
- Maximize hover exposes Windows 11 Snap Layouts.
- Resize cursors and native edge behavior remain unchanged.
- No title-bar control is covered at narrow widths.
- Stroke icons remain sharp, centered, and visually consistent without
  missing-glyph boxes or emoji presentation.
