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
window bitmap while Mica or Acrylic is active. The attribute is disabled again
for the None backdrop.

## Rationale

Qt Quick provides a modern retained UI and integrates custom terminal rendering
into one scene graph. A dedicated native-event layer can preserve Windows
behavior that a purely borderless QML window would otherwise lose.

## Consequences

- Platform-specific window code is expected and isolated.
- Windows behavior is verified on real Windows 11 systems.
- A successful `DWMWA_SYSTEMBACKDROP_TYPE` call is not sufficient evidence:
  runtime checks must also verify the Qt alpha-buffer and transparent-clear
  contract.
- Cross-platform window abstractions are deferred.
- `Qt::FramelessWindowHint` alone is not considered a complete solution.
