# Appearance settings manual verification

Run the dynamic Debug build on Windows 11. Select the Settings shortcut
immediately before the caption buttons and keep a local terminal tab available
for the terminal checks.

## Settings page and persistence

1. Change several controls without selecting **Apply**, then select
   **Discard changes**.
2. Change theme, backdrop style, backdrop opacity, terminal background
   opacity, font, cursor, and behavior controls and select **Apply**.
3. Close and restart ztermy.

Expected:

- The preview updates without changing the active application theme.
- Discard restores the currently saved values.
- Apply updates the application without opening a second window.
- Repeated use of the shortcut reselects one Settings tab rather than creating
  duplicates; closing that tab returns to the previous workspace.
- Restart restores every applied value from `settings.json`.
- Appearance and Terminal switch through the Settings category rail without
  clipping their controls.
- The Settings page remains scrollable and usable at the minimum window size.

## Theme, opacity, and Windows backdrop

Run the real-window DWM state gate in both build shapes:

```powershell
cmake --build --preset msvc-dynamic-debug `
  --target ztermy_window_appearance_runtime_smoke
cmake --build --preset msvc-static-release `
  --target ztermy_window_appearance_runtime_smoke
```

The gate opens the real native window and verifies Dark and Light immersive
mode, rounded corners, Mica/Mica Alt backdrop types, and successful WCA policy
application for Glass and Acrylic. It requires the Qt window itself to remain
at 100% opacity, rejects out-of-range opacity and unknown backdrop tokens, and
checks the terminal title and viewport share one adjustable tint. Glass,
Acrylic and Transparent are adjustable; Mica keeps a fixed palette and Mica
Alt uses a stronger fixed tint.

The native Qt Quick window must request an alpha buffer before its creation,
expose at least one alpha bit, and use a transparent clear color. On Windows
11 build 26100 and later, the gate also requires DWM to accept
redirected-bitmap alpha. A passing gate proves the requested native state
reached DWM and that the scene/compositor path permits the background to be
visible; the visual checks below remain necessary for readability, material
appearance, and interaction review.

1. Place ztermy over a colorful, high-contrast window or desktop background.
2. In Dark and Light themes, compare Glass, Acrylic, Transparent, Mica, and Mica Alt.
3. In Glass, Acrylic and Transparent, compare backdrop opacity at 0%, 50%, and 100%.
4. Confirm the opacity control is unavailable for Mica and Mica Alt.
5. Maximize, restore, snap, and resize after each backdrop change.
6. Apply System theme, then change the Windows app color mode and restart
   ztermy.
7. Compare the ztermy, Follow Windows, and Custom accent sources. While
   Follow Windows is active, change the Windows accent color without
   restarting ztermy. For Custom, try a light color and a dark color.

Expected:

- Text, borders, fields, dialogs, title-bar controls, and focus indicators
  remain readable in both themes.
- System follows the Windows app color mode.
- ztermy preserves the default green palette. Follow Windows updates after a
  Windows accent change, and Custom accepts exactly `#RRGGBB`. Accent text,
  focus, hover, pressed, and selected states remain legible for both light and
  dark accent colors. Success and destructive states keep their semantic
  colors.
- Acrylic continuously blurs and samples content behind the window. Changing
  backdrop opacity changes only its tinting surfaces and never removes the
  live blur by fading the final window.
- Transparent provides ordinary see-through surfaces without Acrylic blur.
  Its opacity control directly changes those background surfaces.
- Mica carries the wallpaper- and theme-derived long-lived material. Mica Alt
  has a stronger tint suitable for the tabbed title-bar hierarchy. Their
  appearance is system controlled and does not expose backdrop opacity.
- Backdrop selection and opacity do not alter terminal cell colors, stored
  terminal content, text opacity, control opacity, or the native window's
  final opacity.
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

## Terminal and title background opacity

1. Place ztermy over a colorful window and select Glass, Acrylic or Transparent.
2. Apply background opacity at 100%, 50%, and 0%.
3. In PowerShell run:

   ```powershell
   $e = [char]27
   Write-Host "$e[41m explicit red background $e[0m normal background"
   ```

4. Select text, type with an IME, and compare the cursor at every opacity.
5. Open and exit a full-screen program such as `hx`, resizing it once.

Expected:

- The terminal title, session strip, pane title and default terminal background
  remain seamless at every opacity.
- At 50% and 0% the whole terminal surface reveals the selected material while
  text remains fully opaque.
- The explicit red ANSI background stays solid even at 0%.
- Selection, cursor, and IME composition remain solid and readable.
- A full-screen program keeps its explicit colors and exits without stale
  cells, misplaced cursor, or a crash.

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

- Defaults are Dark, Acrylic at 100% backdrop opacity, terminal background at
  100% opacity, Cascadia Mono 14 px, terminal-controlled blinking cursor,
  copy-on-select off, and multiline confirmation on.
- The file is versioned JSON and contains no password, passphrase, private-key
  content, terminal input, or clipboard content.
