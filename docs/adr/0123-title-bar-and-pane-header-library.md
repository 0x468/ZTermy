# ADR 0123: title-bar tabs, chrome actions and pane headers share one library

Status: accepted for UI V2

## Context

The title bar drew each of its controls with its own rectangle: the
Workspace, SFTP and Settings tabs (`TitlePageAction`,
`TitleTabNavigationAction`), the terminal tabs (`TerminalTabAction`), the
new-terminal button, the overflow chevron, the quick actions in
`TitleWindowActions` and the caption buttons (`CaptionButton` with Canvas
glyphs). Hover, press, selection and focus were expressed five different
ways, so a selected page tab did not look like a selected terminal tab and
the caption glyphs ignored the icon library. Pane headers lived inside
`TerminalSplitNode` with their own drag proxy, a QML `DropArea` nothing
dropped into, and a third drag ghost next to the two in `Main.qml`.

## Decision

- `TitleTab` is the one title-bar tab. It owns the inset hover pill, the
  opaque selected card (`Theme.tabSelectedBackground`, rounded top corners,
  drawn over the bar hairline so it joins the page), the focus ring, the
  icon/title row and the `KeyboardAction`. Consumers set the width; a
  `trailingInset` reserves room for a close affordance and left-aligns the
  content, otherwise the content centres. `iconSlot` hosts a custom glyph
  (the brand mark) and `focusAction()` is the focus hand-back after dialogs.
- `TitleChromeAction` is the one flat bar command: hover and press fill the
  bar height like a caption button, `menuOpen` keeps the fill while a menu
  is open, `focusTarget` is the item to hand focus back to, `actionInset` 0
  keeps the full hit box on a narrow control, and badges or menus are
  declared as children.
- `TerminalTabAction` is a `TitleTab`; `TitleTabOverflow` and every quick
  action are `TitleChromeAction`s. The `objectName`s and widths the runtime
  smokes read are unchanged.
- `CaptionButton` draws `window-minimize`, `window-maximize`,
  `window-restore` and `window-close` from the icon library. Win32 hit
  testing, Snap Layouts hover and the `surfaceColor`/`hovered` contract are
  unchanged.
- `SessionStatusDot` is the one session-state dot: accent while running,
  pulsing on `Motion.emphasis` while connecting, subtle ink otherwise.
- `TerminalPaneHeader` is the pane strip. It sits inside the pane so the
  active-pane accent edges (`PaneFocusEdges`, drawn only on edges shared with
  a sibling pane) enclose it, paints the divider hairline, and
  exposes `paneId`, `paneTitle` and `dragAreaWidth` for the drag capture in
  `Main.qml`. In a detached window its `DragHandler` calls `startSystemMove`
  for a press left of the pane actions. The QML `DropArea` and
  `managedPaneDrag` are removed: the main window has exactly one pane drag
  path, the capture layer in `Main.qml`.
- `DragPreview` (elevation 2, accent hairline, icon and title) is the one
  drag ghost for tab reorder and pane drag; `DropTargetIndicator` is the one
  drop highlight (accent insert bar or tinted merge area). The indicator
  slides while shown and holds its geometry while fading out.
- `Theme.tabSelectedBackground` is a skin token, lighter than the chrome in
  both skins because the light chrome tint equals `controlBackground`.

## Consequences

- A new title-bar control is a `TitleTab` or a `TitleChromeAction`; nothing
  else draws hover, press or selection in the bar.
- Pane headers, the tab strip and the overflow menu share the status dot, so
  a session state change looks the same everywhere it is shown.
- `--window-appearance-smoke` asserts the ADR 0120 surface contract (chrome
  and workspace alpha follow the material, content surfaces stay opaque)
  instead of the pre-V2 whole-window alpha ladder it still encoded.
- `TitlePageAction` and `TitleTabNavigationAction` no longer exist; older
  acceptance notes that name them describe history.
