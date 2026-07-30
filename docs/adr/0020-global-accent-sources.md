# ADR 0020: Global accent sources

## Status

Accepted

## Context

ztermy V1 used a fixed green accent. V1.1 needs a personalizable application
accent without duplicating appearance settings across SSH profiles or
assigning one arbitrary color to every interaction state.

Windows exposes the current colorization color through DWM and notifies
top-level windows when it changes. Custom colors also need a stable,
portable representation in the versioned application settings document.

## Decision

The application has one global accent source:

- **ztermy** preserves the established green palette and remains the default;
- **Follow Windows** reads the current DWM colorization color and updates on
  `WM_DWMCOLORIZATIONCOLORCHANGED` and relevant settings changes;
- **Custom** stores one validated `#RRGGBB` color.

`NativeWindow` exposes the Windows accent as a Qt color. `Theme.qml` owns the
derived hover, pressed, focus, selected-background, and contrast-text roles.
For non-ztermy sources, accent text is chosen from black or white using the
WCAG relative-luminance threshold. Semantic success, warning, danger, and
destructive roles do not follow the accent.

Accent settings are stored in application-settings schema version 4. Older
documents migrate to the ztermy source and `#22C55E` custom-color default.
Accent settings remain global and are not added to SSH profiles.

## Consequences

- Existing users retain the exact ztermy green appearance after migration.
- Windows accent changes can appear without restarting ztermy.
- Arbitrary custom colors do not make primary-button text unreadable.
- One source color does not erase semantic status distinctions.
- Native DWM lookup failure falls back to the standard Windows blue until a
  later notification supplies a valid system color.
