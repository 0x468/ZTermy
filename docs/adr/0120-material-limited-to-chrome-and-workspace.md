# ADR 0120: window material is limited to chrome and terminal workspace

Status: accepted for UI V2

## Context

The native window applies one DWM material (Mica, Mica Alt, Acrylic or a
transparent swapchain) to the whole client area. Every QML surface then
re-derives its own alpha (`panelAlpha`, `chromeAlpha`, `controlAlpha`,
`fieldAlpha`, ...) so that cards and controls stay readable over the material.
Controls that need to look like controls end up opaque while the page behind
them is translucent, which reads as a seam whenever the backdrop opacity is
lowered. Settings, Hosts, AI and SFTP pages carry a dozen alpha formulas each
and still look inconsistent.

## Decision

- The material shows through only the title bar, the tab strip and the
  terminal workspace (pane gaps and the terminal background where the theme
  allows it).
- Content pages, side drawers, popups, menus, tool tips, toasts and dialogs
  paint an opaque panel from the theme skin. Depth is expressed with elevation
  (shadow plus hairline), never with stacked alpha.
- `Theme` exposes one `effectsTier` (`full`, `reduced`, `off`). `full` keeps
  the material, shadows and full motion; `reduced` drops shadows and shortens
  motion; `off` paints solid surfaces without motion. The Windows "animate
  controls" preference forces motion to `reduced` or `off` but does not change
  the material.
- The per-surface alpha formulas are removed from `Theme`; surfaces read solid
  colors from the skin.

## Consequences

- Backdrop opacity only affects the chrome and workspace, so lowering it can
  no longer make settings text unreadable.
- Controls no longer need special-case tints to stand out from a translucent
  page.
- Existing `backdropOpacity` semantics change: the value now scales the
  workspace/chrome tint only. This branch does not keep backward
  compatibility for the previous whole-window behaviour.
