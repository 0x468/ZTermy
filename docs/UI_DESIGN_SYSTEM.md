# ztermy V1 UI design system

Status: accepted for V1 convergence

## Product character

ztermy is a compact Windows 11 desktop tool. The terminal is the primary
content, while application chrome, host management, and settings should remain
quiet, predictable, and easy to scan.

The V1 direction is:

- professional and dark-first, with a complete light appearance;
- compact rather than spacious or presentation-oriented;
- neutral surfaces with green reserved for connection, readiness, focus, and
  primary actions;
- high contrast without neon glow, glass decoration, or terminal-themed
  gimmicks;
- original ztermy presentation built from Qt Quick and native Windows
  behavior.

Netcatty and other SSH tools may inform workflow hierarchy and density only.
Their source, styles, icons, screenshots, text, themes, and branding are not
design-system inputs.

## Semantic tokens

`Theme.qml` is the runtime source of truth. Pages and components use semantic
roles instead of page-owned color literals.

### Surface hierarchy

From lowest to highest:

1. `windowBackground`: native-window clear surface.
2. `chromeBackground`: title bar and fixed application chrome.
3. `panelBackground`: navigation rail and persistent secondary panels.
4. `workspaceBackground`: page and terminal workspace.
5. `elevatedBackground`: cards, editors, prompts, and grouped settings.
6. `floatingBackground`: transient menus and overlays.

`controlBackground`, `controlHover`, and `controlPressed` are interaction
surfaces. They must not be substituted with page-specific blues or greens.

### Content and status

- `text`: primary labels and values.
- `textSoft`: secondary information that still needs strong readability.
- `textMuted`: descriptions and metadata.
- `textSubtle`: tertiary information only; never required instructions.
- `border`: ordinary separation.
- `borderStrong`: focused grouping and floating boundaries.
- `accent`: connected, ready, focus, and primary-action emphasis.
- `danger`: destructive actions and blocking errors.

Color is never the only status signal. Connected, warning, failure, and
selected states also use text, shape, or an accessible name.

### Typography

- Application UI: `Segoe UI Variable`, using the installed Windows font.
- Terminal and fingerprints: the configured terminal font, defaulting to
  `Cascadia Mono`.
- Do not download web fonts or bundle an unrelated display font for V1.
- Page title: 20 px, demi-bold.
- Section title: 16 px, demi-bold.
- Body and controls: 13 px.
- Labels and metadata: 11 px.
- Compact status text: 9 px only when the same information is available
  accessibly elsewhere.

### Geometry

Use a four-pixel base rhythm.

- Dense gap: 4 px.
- Control-internal gap: 8 px.
- Related-control gap: 12 px.
- Section gap: 16 px.
- Card inset: 20 px.
- Page inset: 28 or 32 px.
- Small radius: 4 px.
- Control radius: 8 px.
- Panel and dialog radius: 12 px.
- Standard title bar: 42 px.

Terminal cell geometry is independent from application density.

## Component states

Every interactive component defines these states without changing its outer
geometry:

1. Disabled
2. Pressed
3. Hovered
4. Selected or checked
5. Default

Keyboard focus is a separate visible overlay and must remain visible on
selected, hovered, and destructive controls. Hover and focus may change color
or border opacity, but must not scale, translate, or reflow surrounding
content.

Mouse and keyboard activation share one action signal. Enter, Return, and
Space activate button-like controls once and ignore key auto-repeat.

## Component inventory

### Existing foundations

- `Theme`: all semantic visual tokens.
- `CaptionButton`: native-title-bar commands while preserving Win32 hit
  testing and Snap Layouts.
- `KeyboardAction`: mouse, keyboard, focus, accessibility, and pointer
  behavior for self-drawn actions.
- `HostKeyPrompt`: modal host-identity security boundary.

### V1 convergence components

The convergence pass should extract these reusable primitives before
duplicating another page-specific implementation:

- action button: default, primary, and destructive variants;
- text and secret field with label, validation, and error announcement;
- section card with title and optional description;
- inline status message for information, success, warning, and error;
- confirmation dialog with initial focus and safe Escape behavior;
- compact navigation item and tab action;
- empty, loading, disconnected, and recoverable-error state.

Controls from Qt Quick Controls remain valid where their native interaction
model is useful. A wrapper should normalize palette, focus, metrics, and
accessibility rather than reimplement text editing, combo boxes, or sliders.

## Keyboard and accessibility

- Tab order follows visible reading order.
- No functional action is pointer-only.
- Page changes move focus to the page's meaningful first action or terminal
  viewport.
- Modal dialogs trap focus until accepted or dismissed and restore focus to
  their invoker.
- Destructive dialogs default to the safe action.
- Errors are exposed through accessible names or announcements, not only red
  borders.
- Fingerprints remain selectable and are announced as one value.
- Custom controls provide an appropriate `Accessible.role` and
  `Accessible.name`.

The title bar, host vault, terminal tabs, search, failure recovery, settings,
and all dialogs require a keyboard-only acceptance pass.

## Motion

- Ordinary hover and color feedback uses at most 120 ms.
- No scale-on-hover, parallax, scroll hijacking, glow, glitch, or continuous
  decorative animation.
- Terminal rendering and cursor behavior are not driven by decorative QML
  animations.
- When Windows disables client-area animation, nonessential application
  transitions must become immediate.

Respecting the Windows animation preference is a remaining V1 implementation
item; adding a separate application-only toggle is not a substitute.

## Layout behavior

- The terminal viewport always receives the remaining workspace rather than a
  fixed presentation size.
- Narrow layouts preserve the terminal first and collapse or scroll secondary
  content.
- Dialog width is bounded by the current window and keeps at least 24 px from
  each edge.
- Text wraps instead of clipping required instructions.
- Hover, error, and focus states do not change layout size.
- No terminal cells, scrollback lines, or selection cells are represented as
  QML object trees.

## Performance boundary

- A terminal viewport remains one custom scene-graph item.
- Shared application controls may be QML objects; terminal content may not.
- Page transitions must not force terminal snapshots or texture uploads.
- Visual convergence is accepted only after local and SSH input latency and
  sustained-output checks still pass.

## V1 visual acceptance

For each primary screen, verify:

- dark, light, and system theme;
- normal, narrow, maximized, and snapped geometry;
- 100%, 125%, 150%, and 200% display scale where available;
- mouse, keyboard-only, and screen-reader-accessible names;
- default, hover, pressed, focused, selected, disabled, error, and connected
  states as applicable;
- no clipped required text or layout movement during interaction;
- no regression to Snap Layouts, IME placement, terminal latency, or resize.

