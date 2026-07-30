# ADR 0001: Windows-native Qt Quick application shell

Status: accepted

## Decision

V1 targets Windows 11 x64 and uses Qt Quick/QML for application presentation
with C++23 for application logic and Windows integration.

The main window uses a visually custom title bar backed by native Win32/DWM
behavior. The implementation retains native window-management semantics,
including resize hit testing and Snap Layouts.

System backdrops use a transparent Qt Quick clear color and an alpha-capable
surface created before the first `QQuickWindow`. On Windows 11 build 26100 and
later, the native layer also enables premultiplied alpha for the redirected
window bitmap while Acrylic, Transparent, Mica, or Mica Alt is active.

The QML design system keeps the root surface transparent while a material is
active and composes readable semi-transparent application surfaces above it.
Mica uses stable surface tints and Mica Alt uses a stronger fixed tint.
Acrylic and Transparent expose background-surface opacity; Acrylic keeps DWM's
behind-window blur and sampling, while Transparent does not request a material.
The native window remains fully opaque so text, controls, and the final DWM
effect are never faded as a group.

## Rationale

Qt Quick provides a modern retained UI and integrates custom terminal rendering
into one scene graph. A dedicated native-event layer can preserve Windows
behavior that a purely borderless QML window would otherwise lose.

## Consequences

- Platform-specific window code is expected and isolated.
- Windows behavior is verified on real Windows 11 systems.
- A successful `DWMWA_SYSTEMBACKDROP_TYPE` call is not sufficient evidence:
  runtime checks must also verify the Qt alpha-buffer and transparent-clear
  contract, all four DWM mappings, and mode-appropriate surface palettes.
- Cross-platform window abstractions are deferred.
- `Qt::FramelessWindowHint` alone is not considered a complete solution.
