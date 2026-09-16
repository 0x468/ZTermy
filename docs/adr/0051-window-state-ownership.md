# ADR 0051: One owner for top-level window state transitions

## Status

Accepted

## Context

ztermy changes a top-level window's minimized/maximized state from many places:
the custom title-bar buttons, the detached-window caption buttons, the tray
"show" command, single-instance activation, and workspace reattach/merge
flows. Each caller used a different `QWindow::show*()` helper.

`QWindow::showNormal()`, `showMinimized()`, and `showMaximized()` replace the
whole state set, and `QWindow::show()` is `showNormal()` on Windows. Minimizing
a maximized window with `showMinimized()` therefore stores `WindowMinimized`
alone; the Qt Windows platform plugin then clears the native
`WPF_RESTORETOMAXIMIZED` placement flag, so every later restore, whether from
the taskbar, the tray, or a second launch, brings the window back as a normal
window. Fixing one caller does not fix the others, and new callers reintroduce
the defect.

## Decision

`src/core/windowing` is the only owner of these transitions:

- `WindowStateTransitions` holds the pure rules. Each rule owns one flag and
  preserves every other flag: minimize adds `WindowMinimized`, present removes
  it, and the maximize toggle flips `WindowMaximized` while clearing
  `WindowMinimized`.
- `WindowPresenter` applies a rule to a `QWindow` through `setWindowStates()`
  plus `setVisible(true)`, never through `show*()`. `reveal()` makes a hidden
  window visible without an explicit raise or activation request; `present()` additionally
  raises and activates the window for explicit foreground actions.

QML reaches the presenter through the `WindowControl` singleton; C++ callers
(`NativeWindow` tray restore, `ApplicationInstance` activation) call the
presenter directly. Runtime smokes may still drive absolute states because they
verify the platform, not the product policy.

`scripts/code_health_report.ps1` rejects `show()`, `showNormal()`,
`showMinimized()`, `showMaximized()`, `showFullScreen()`, and
`setWindowState(s)()` outside the owner module and the named runtime-smoke
helpers in `WindowStateRuntimeSmoke.h`; `src/main.cpp` has no blanket
exemption.

## Consequences

- Maximize survives minimize, hide-to-tray, single-instance activation, and
  workspace transfers for both the main window and detached windows.
- Adding a window-related entry point means calling the presenter; the gate
  fails builds that bypass it.
- Native paths (`WM_SYSCOMMAND` from the non-client maximize button, taskbar
  and `Win+Down` minimize) remain native and already preserve the flag.
- The pure rules and the `QWindow` behaviour are unit tested on the offscreen
  platform; the Windows placement flag is checked by
  `ztermy_window_runtime_smoke`.
