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
- Window appearance and Terminal cards share the same inset, heading,
  background, border, and corner radius without clipping their controls.
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

## Host Vault theme states

1. Save at least one private-key profile and one password profile.
2. In Dark theme, hover a host card, open the authentication selector, show a
   successful status, and open the delete confirmation.
3. Repeat the same states in Light theme.
4. Use Tab to move focus through the card actions and dialog buttons in both
   themes.

Expected:

- Cards, authentication menus, selected rows, success text, destructive
  borders, and destructive buttons use the active theme instead of retaining
  dark-only colors.
- Primary and destructive button text remains readable.
- Keyboard focus remains visible over hovered, selected, primary, and
  destructive states.
- Changing theme causes no card movement, clipped text, or modal layout shift.

## Windows animation preference

1. Open Windows **Settings > Accessibility > Visual effects** while ztermy is
   running.
2. Turn **Animation effects** off.
3. Move the pointer repeatedly across the minimize and close caption buttons.
4. Turn **Animation effects** on and repeat without restarting ztermy.

Expected:

- With animation effects off, nonessential caption color transitions are
  immediate and controls remain fully functional.
- With animation effects on, the same transition is subtle and no longer than
  120 milliseconds.
- The preference takes effect while ztermy is running.
- Snap Layout hover, maximize/restore, focus indicators, terminal cursor
  behavior, and terminal rendering are unchanged.

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
