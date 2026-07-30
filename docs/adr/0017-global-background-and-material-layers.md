# ADR 0017: Global background and material layers

Status: accepted

## Context

Windows 11 offers Acrylic, Mica, Mica Alt, and unblurred transparent window
backgrounds, but a native backdrop is only the lowest visual layer. QML page,
card, control, and terminal backgrounds can still cover that material.
Applying native whole-window opacity would make text, icons, the terminal
cursor, and accessibility affordances fade together with the background.

Terminal applications also need to distinguish the default terminal
background from explicit ANSI cell backgrounds. Treating both as ordinary
application surfaces can make full-screen programs such as editors visually
incorrect.

ztermy stores SSH profiles to describe connections. Giving every SSH profile
its own complete appearance would add editing, migration, and troubleshooting
cost without serving the Windows 11-first personal-tool goal.

## Decision

Window and terminal appearance are global application preferences. SSH
profiles do not own themes, backdrop modes, background images, or opacity.
A future lightweight host color or icon may identify a connection, but it
must not duplicate the global appearance model.

V1 keeps the existing ztermy green semantic accent. A V1.1 appearance
extension may add a global accent source with ztermy, Follow Windows, and
Custom choices. A Windows or custom color must be transformed or rejected
when it cannot preserve text, focus, selected, and destructive-state
contrast; ztermy green remains the fallback.

The visual stack has three independently owned layers:

1. `NativeWindow` applies one Windows system backdrop to the whole client
   area: Acrylic, Transparent, Mica, or Mica Alt.
2. `Theme.qml` supplies material-aware QML surface tints for window chrome,
   navigation, workspaces, cards, fields, controls, and popups.
3. The terminal renderer owns terminal default and explicit ANSI cell
   backgrounds.

Acrylic and Transparent expose one global background-opacity value. Zero is
fully transparent and one hundred percent is fully opaque for the large
application background surfaces. The value never changes native whole-window
opacity and therefore never fades text, icons, or terminal glyphs.

Cards and controls remain visible at zero background opacity. Their semantic
surface roles use progressively stronger minimum tints:

- page, chrome, and navigation backgrounds follow the configured opacity
  exactly;
- cards and grouped surfaces retain a readable material tint;
- buttons and fields retain a stronger tint;
- transient popups remain the strongest application surface.

Light surfaces keep a higher minimum tint than dark surfaces. Focus, hover,
pressed, selected, disabled, and validation states keep their existing
semantic roles and do not change geometry.

Mica and Mica Alt do not expose opacity. Their QML tint strengths are fixed,
with Mica Alt using the stronger hierarchy. The selected Windows material
remains responsible for blur, luminosity, and wallpaper sampling; QML does
not create a separate blur region for each control.

The Settings page previews draft window appearance on the whole application
window. This is the actual native material and QML surface stack, not a
simulated sample inside a card. Applying persists the draft; discarding or
leaving Settings restores the persisted appearance.

Terminal background transparency, background images, and explicit ANSI-cell
background opacity will be implemented as global terminal-appearance
settings. They are separate from window background opacity. The existing
opaque terminal background remains the default until that work is accepted.

## Consequences

- Transparent and Acrylic have predictable percentage semantics: 100% means
  visually opaque application backgrounds.
- Material remains visible through cards and grouped settings without making
  their content unreadable.
- Mica behavior is not distorted by an unrelated opacity slider.
- Terminal text and application controls are never faded by window opacity.
- Windows accent synchronization remains a bounded global V1.1 feature rather
  than an SSH-profile option.
- A future terminal-background feature must distinguish default background
  cells from explicit ANSI backgrounds and must be tested with full-screen
  terminal programs.
- Runtime acceptance is still required for Windows composition, light and
  dark appearances, transparency disabled in Windows Settings, resize, IME,
  and terminal latency.
