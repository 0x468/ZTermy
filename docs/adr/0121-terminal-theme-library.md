# ADR 0121: terminal theme library with a palette layer and a chrome skin

Status: accepted for UI V2

## Context

The terminal engine rendered with Ghostty's built-in palette and the QML
`Theme` singleton carried the chrome colors as literals. There was no way to
ship or import a terminal color scheme, the selection colors were hard-coded
in `TerminalItem`, and the workspace tint could drift from the terminal
background whenever one of them changed.

## Decision

- `TerminalColorScheme` (core/config) is the palette contract: default
  foreground, background, cursor and the 16 ANSI colors. `TerminalEngine`
  gains `setColorScheme`; Ghostty keeps its 6x6x6 cube and gray ramp and only
  the 16 ANSI entries plus the defaults are replaced. Sessions queue a
  `ColorSchemeCommand` and reapply the scheme when the engine restarts.
- `TerminalThemeCatalog` (core/config) owns the theme list: built-in themes
  (`ztermy-dark`, `ztermy-light`, One Dark, Dracula, Solarized, Gruvbox, Nord,
  Catppuccin Mocha, Tokyo Night) plus custom themes stored as ztermy JSON
  under `<data>/themes/<id>.json`. Import accepts ztermy JSON, Windows
  Terminal scheme objects or a `settings.json` with `schemes`, and Ghostty
  theme files. Ids are slugs; collisions get a numeric suffix.
- Settings persist `terminalTheme` (schema 35). Unknown ids resolve to the
  built-in default for the current darkness rather than failing the load.
- `AppController` exposes `terminalThemeId`, `terminalThemeColors`,
  `terminalThemes` and `saveTerminalTheme` / `previewTerminalTheme` /
  `endTerminalThemePreview` / `importTerminalThemeFile` /
  `removeTerminalTheme`. A preview pushes the scheme to every live session
  without persisting; ending it restores the persisted theme.
- `Theme.qml` is split in two layers. The palette layer
  (`terminalPalette`, `terminalBackground`, `terminalSelection*`,
  `terminalAnsi`, `terminalAccentHint`) is bound from the controller. The
  chrome skin derives only the workspace fill, the selection pair and, for
  the "ztermy" accent choice, an optional accent hint from it. Chrome surfaces
  never read the ANSI table.
- `TerminalItem` takes `selectionBackground` / `selectionForeground` as
  properties; `TerminalSplitNode` binds them from `Theme`.
- The picker is a dialog on elevation 3 with swatch cards; hovering a card
  previews it, Apply persists, Cancel/Escape ends the preview. Import and
  Remove live in the dialog; built-in themes cannot be removed.

## Consequences

- Terminal color changes go through one path (engine option set under the
  engine mutex followed by a snapshot), so the viewport stays a single
  scene-graph item and no page transition forces a snapshot.
- Custom themes are plain files; users can copy them between machines.
- `Theme` still holds the chrome skin literals; a later chapter may move the
  skin into theme data as well, using the same palette/skin boundary.
