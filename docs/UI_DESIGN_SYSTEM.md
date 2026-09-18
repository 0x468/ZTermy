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

`Theme` has two layers (ADR 0121). The **terminal palette** (`terminalPalette`,
`terminalBackground`, `terminalForeground`, `terminalCursor`,
`terminalSelectionBackground/Foreground`, `terminalAnsi`) is bound from
`AppController.terminalThemeColors`, which resolves the persisted
`terminalTheme` id (or a running preview) against `TerminalThemeCatalog`. The
**chrome skin** (surface ladder, ink ladder, accent family, status colors) is
derived: `workspaceBackground` tints the terminal background, the selection
pair goes to `TerminalItem`, and the "ztermy" accent choice follows the
theme's accent hint when it has one. Chrome never reads the ANSI table.

### Surface hierarchy

From lowest to highest:

1. `windowBackground`: native-window clear surface.
2. `chromeBackground`: title bar and tab strip; the only chrome that shows
   the Windows material.
3. `workspaceBackground`: terminal workspace; also shows the material.
4. `contentBackground`: opaque page body (Hosts, Settings, AI, SFTP, logs).
5. `panelBackground`: navigation rail and docked side panels (elevation 0).
6. `elevatedBackground`: cards, editors and state panels (elevation 1).
7. `floatingBackground`: menus, tool tips, popovers, toasts, drawers and
   dialogs (elevation 2 and 3).

`controlBackground`, `controlHover`, and `controlPressed` are interaction
surfaces. They must not be substituted with page-specific blues or greens.

Every opaque surface is an `AppSurface { elevation: n }` (ADR 0120). The
component owns the fill, hairline (`border` below elevation 2, `borderStrong`
from elevation 2), radius (`radiusPanel`, or `radiusControl` when `compact`)
and the shadow. Elevation 2 casts a short shadow, elevation 3 a deeper one;
both are dropped when `Theme.shadowsEnabled` is false (`reduced`/`off` tiers
and high contrast). Nothing else may draw a shadow or a translucent fill.
Depth comes from elevation, never from stacked alpha.

The Windows backdrop is one native layer behind the chrome and workspace only.
Acrylic and Transparent background opacity scales exactly those two tints: 0%
is transparent and 100% is opaque. Mica and Mica Alt use fixed tints and do
not expose an opacity control. Content pages never reveal the material, so
lowering the opacity cannot make settings text unreadable.

Radii come from four tokens: `radiusSmall` (chips, focus rings, drop
targets), `radiusCompact` (tab and pane affordances), `radiusControl`
(buttons, fields, menus, navigation rows) and `radiusPanel` (cards, dialogs).
Dots, pills and round buttons use `height / 2`; numeric radii are not
allowed in QML.

Draft window appearance is previewed live on the whole native window. A QML
child cannot reveal the Windows backdrop through already painted ancestors,
so material previews must not imitate Acrylic or Mica inside an isolated
opaque card. Apply persists the draft; Discard or leaving Settings restores
the saved appearance.

Window appearance and terminal appearance are global. SSH profiles do not own
themes, backdrop modes, background images, or opacity. Terminal default
backgrounds, explicit ANSI cell backgrounds, and future background images
remain renderer-owned layers rather than application-control colors.

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

V1.1 provides one global accent source with ztermy, Follow Windows, and Custom
choices. System and custom colors use a
contrast-safe derived palette for hover, pressed, focus, selected, and accent
text roles; raw Windows color values are not assigned to every role. Semantic
success and danger colors remain independent from the chosen accent.

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
- Presentation-card inset: 20 px; use only for About/release identity, bounded
  state, or security content that genuinely needs an elevated container.
- Tool-surface inset: 8 or 12 px.
- Functional-page inset: 12 or 16 px. About may use 28 or 32 px.
- Small radius: 4 px.
- Control radius: 8 px.
- Panel and dialog radius: 12 px.
- Standard visual title bar: 38 px, while native caption actions preserve
  their established horizontal targets and Win32 hit-test behavior.

Terminal cell geometry is independent from application density.

V2.2 adds a density correction: ordinary toolbars and setting rows target
28-36 px, and regular-width host items target 64-72 px. Hosts, SFTP, history,
command snippets, and non-Application settings must not use page heroes or
large cards as their default organization primitive.

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
- `AppIcon`: ztermy-owned, stroke-based vector icons that stay independent
  from platform font glyphs, emoji, and third-party icon assets.
- `ActionButton`: default, primary, and destructive application actions with
  shared mouse, keyboard, focus, disabled, and reduced-motion behavior.
- `AppTextField`: standard and compact editable fields that retain Qt input,
  selection, validator, IME, and secret-echo behavior.
- `AppComboBox`, `AppSpinBox`, and `AppSlider`: shared choice and numeric
  controls that preserve Qt keyboard/editing semantics while normalizing
  geometry, focus, popup, hover, and theme roles.
- `AppSwitch` and `AppCheckBox`: shared boolean controls with stable track or
  indicator geometry, explicit labels, visible focus, and semantic enabled,
  checked, hover, and disabled states.
- `StatusMessage`: information, success, and alert presentation with semantic
  color and accessibility roles.
- `ConfirmationDialog`: bounded modal confirmation with semantic primary or
  destructive action, safe initial focus, Escape rejection, and focus
  restoration.
- `SectionCard`: shared elevated grouping with consistent heading,
  description, inset, border, radius, and semantic theme colors.
- `StatePanel`: shared empty, loading, disconnected, and recoverable-error
  presentation with semantic status, wrapped guidance, optional actions, and
  accessible announcements.
- `SideNavigationItem`: compact sidebar navigation with selected, hover,
  keyboard-focus, and accessible-button states.
- `TerminalTabAction`: bounded title-bar terminal action with session status,
  activation, close, keyboard focus, and accessible names.
- `CaptionButton`: native-title-bar commands while preserving Win32 hit
  testing and Snap Layouts.
- `KeyboardAction`: mouse, keyboard, focus, accessibility, and pointer
  behavior for self-drawn actions.
- `HostKeyPrompt`: modal host-identity security boundary.

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

Motion is expressed by role through the `Motion` singleton; QML never writes
a numeric duration or picks an easing curve on its own.

| Role | Use | Full | Reduced |
|------|-----|------|---------|
| `feedback` | hover/press colour, opacity, indicator moves | 120 ms | 72 ms |
| `enter` | popups, menus, tool tips, toasts, dialogs appearing | 200 ms | 120 ms |
| `exit` | the same surfaces leaving | 120 ms | 72 ms |
| `relocate` | widths, margins, drawer slides, tab reordering | 200 ms | 120 ms |
| `page` | page and settings-category reveals | 220 ms | 132 ms |
| `emphasis` | connecting pulses and other continuous cues | 360 ms | 216 ms |

Shared components wrap the roles: `MotionColor`, `MotionFeedback` and
`MotionRelocate` inside `Behavior`, `MotionEnter`/`MotionExit` as popup
transitions, `MotionReveal` for 0..1 reveal properties. Entering surfaces fade
while settling from `Motion.revealScale` (97%) and pages travel
`Motion.distance` (8 px); reduced motion keeps the fade and drops the travel.
The `off` tier and the Windows client-area animation preference set every
duration to 0, so transitions become immediate without per-site guards.

- No scale-on-hover, parallax, scroll hijacking, glow, glitch, or continuous
  decorative animation outside the `emphasis` role.
- Terminal rendering and cursor behavior are not driven by decorative QML
  animations.

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
