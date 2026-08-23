# ADR 0109: Opaque surface performance mode

- Status: Accepted
- Date: 2026-08-23

## Context

Qt Quick can remain responsive on modest physical hardware while feeling uneven on GPU-less cloud desktops or software
renderers. Selecting a visually opaque color was not a valid isolation test: ztermy still requested an alpha-capable
surface, cleared the native window transparently, extended the DWM frame, and enabled redirection-bitmap alpha. Windows
therefore continued to compose a transparent-capable window even when every QML layer happened to paint at 100 percent
opacity.

Users need a reversible way to remove that composition cost without degrading terminal correctness, SSH/SFTP behavior,
font shaping, or retained workbench state.

## Decision

ztermy provides two related controls:

1. `No material (solid)` is a backdrop choice. After restart it creates an opaque Qt Quick surface, disables the DWM
   system backdrop and redirection alpha, and uses zero extended-frame margins.
2. `Prioritize performance on software-rendered or low-power machines` forces the same opaque native surface and also
   disables decorative animation. It preserves the user's selected backdrop and restores it when the mode is disabled.

Both transitions require a restart because the alpha-buffer contract is selected before the first `QQuickWindow` is
created. The application settings schema stores the mode independently from the selected backdrop. Existing settings
default to the mode being disabled.

The mode does not reduce terminal frame rate, change terminal rendering fidelity, disable background I/O, or silently
alter SSH/SFTP behavior. Further reductions require separate measurements and decisions.

## Consequences

- A solid-looking window is now a real opaque-surface experiment rather than a cosmetic approximation.
- GPU-less and remote environments can isolate DWM material/composition overhead with one setting.
- The About-page release animation is intentionally stationary while performance mode is active.
- If interaction remains slow after restart in performance mode, investigation should focus on Qt's selected graphics
  backend, remote-desktop presentation, GUI/render-thread work, and machine resources rather than backdrop material.
- Runtime appearance smoke tests cover the solid DWM and alpha contract; physical cloud-desktop acceptance remains a
  manual evidence gate.
