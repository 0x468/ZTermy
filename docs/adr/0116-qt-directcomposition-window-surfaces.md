# ADR 0116: One composition surface for Qt Direct3D windows

- Status: Accepted for V5; full desktop acceptance remains tracked in V5.4
- Date: 2026-09-12

## Evidence

The detached terminal retained a light rectangle at its original window bounds
after maximizing. Its Qt scene image was uniform, but the desktop-composited image
was not. Hiding the original main window did not remove the rectangle. The fixture
uses no terminal process or SSH connection and has a controlled opaque backdrop.

Changing glass margins, reapplying DWM settings after geometry/presentation,
disabling legacy blur, and disabling attribute 39 separately did not fix the pixel
assertion. An early visual interpretation that full-client margins helped was
incorrect: exact samples remained 133/137/141 inside versus 48/52/56 outside.
Those ineffective experimental implementations were removed.

Qt 6.8.3's `qrhid3d11.cpp` uses DirectComposition for translucent flip swapchains.
Its Windows platform plugin provides `QT_QPA_DISABLE_REDIRECTION_SURFACE`, which
sets `WS_EX_NOREDIRECTIONBITMAP` and is explicitly limited to D3D surfaces. With
that mode, the former rectangle and its surroundings both measured 44/48/52.
The default-startup integration passes the same desktop and scene assertions.

## Decision

Before ztermy creates native windows, enable the existing Qt platform option for
Direct3D 11/12 when legacy non-flip mode is not requested. Respect an explicitly
set environment override. Do not apply this default to OpenGL or software scene
graph rendering. This affects only the current process, not the user's system or
stored settings. Recheck the Qt platform option when upgrading Qt.

Do not mix an extra GDI redirection bitmap into the DirectComposition-backed
window. Retain the existing native backdrop and frame-margin policy; do not add
resize timers, event interception, or repaint loops to conceal the artifact.

Also keep an alpha-capable window's requested alpha format stable when changing
its clear color to opaque. Qt's `QQuickWindow::setColor` otherwise changes the
requested alpha size to -1 while its existing alpha swapchain survives, generating
a consistency warning. Startup opaque/performance windows retain their separate
opaque policy. Solid painting does not require recreating an alpha-capable window.

## Verification boundaries

`--detached-material-smoke --data-dir <isolated directory>` captures both raw Qt
frames and the actual desktop client region, not a translucent HWND's GDI DC.
It checks acrylic/solid, maximization, hidden parent, restoration, independent
window ownership, alpha format and body pixels. It cannot by itself certify
cross-monitor movement, real session reattachment, taskbar behavior or IME.

See `docs/V5_EXPERIENCE_CONVERGENCE.md` for exact run evidence and remaining checks.
No Tab/Pane/Session ownership model was changed.
