# Appearance settings manual verification

Run the dynamic Debug build on Windows 11. Open **Hosts**, select
**Settings** in the sidebar, and keep a local terminal tab available for the
terminal checks.

## Settings page and persistence

1. Change several controls without selecting **Apply**, then select
   **Discard changes**.
2. Change theme, opacity, backdrop, font, cursor, and behavior controls and
   select **Apply**.
3. Close and restart ztermy.

Expected:

- The preview updates without changing the active application theme.
- Discard restores the currently saved values.
- Apply updates the application without opening a second window.
- Restart restores every applied value from `settings.json`.
- The Settings page remains scrollable and usable at the minimum window size.

## Theme, opacity, and Windows backdrop

1. Apply Dark and Light themes at 100% opacity.
2. Apply System theme, then change the Windows app color mode and restart
   ztermy.
3. At 75% opacity, compare None, Mica, and Acrylic backdrops.
4. Maximize, restore, snap, and resize after each backdrop change.

Expected:

- Text, borders, fields, dialogs, title-bar controls, and focus indicators
  remain readable in both themes.
- System follows the Windows app color mode.
- Opacity affects the application window; backdrop selection does not alter
  terminal cell colors or stored terminal content.
- Unsupported DWM attributes degrade to a normal background without a crash.
- Snap Layouts, native resizing, maximized work-area sizing, and custom
  caption buttons remain functional.

## Terminal typography and cursor

1. Apply font sizes 8, 14, and 32 using an installed monospaced font.
2. Apply Terminal controlled, Block, Bar, and Underline cursor styles.
3. Toggle cursor blinking.
4. Resize the terminal after every font-size change.

Expected:

- Font changes update terminal geometry and the shell receives the new row
  and column count.
- Text remains aligned to the terminal grid with CJK cells still occupying
  their correct width.
- Each forced cursor style is visibly distinct; Terminal controlled respects
  the active terminal cursor style.
- Disabling blink leaves the cursor continuously visible.

## Selection and paste behavior

1. Enable copy-on-select, select terminal text, and paste it into a local
   non-sensitive text field.
2. Disable copy-on-select and repeat with a different selection.
3. Enable multiline-paste confirmation, copy two harmless lines, and press
   `Ctrl+Shift+V` in the terminal. Cancel once, then repeat and confirm.
4. Disable the confirmation and paste the same harmless lines again.

Expected:

- Copy-on-select updates the clipboard only after a dragged selection.
- With copy-on-select disabled, selection alone does not replace clipboard
  content.
- The confirmation reports only the number of lines; it does not display or
  log clipboard content.
- Cancel sends no bytes. Confirm sends the pending bytes exactly once.
- With confirmation disabled, the paste follows the normal terminal path.

## Reset and storage safety

Select **Reset defaults**, restart, and inspect the active data mode's
`settings.json`.

Expected:

- Defaults are Dark, 100% opacity, no backdrop, Cascadia Mono 14 px,
  terminal-controlled blinking cursor, copy-on-select off, and multiline
  confirmation on.
- The file is versioned JSON and contains no password, passphrase, private-key
  content, terminal input, or clipboard content.
