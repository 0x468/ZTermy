# ADR 0120: window material is limited to chrome and terminal workspace

Status: accepted for UI V2

## Context

The native window applies one DWM material (Mica, Mica Alt, Glass, Acrylic or a
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
- Performance mode preserves the selected effects tier but makes `off` the
  effective tier for that launch. The preference is disabled in Settings and
  becomes effective again when performance mode is turned off and restarted.
- Glass uses `DwmEnableBlurBehindWindow` plus the experimental user32
  `WCA_ACCENT_POLICY` blur state. Acrylic uses the corresponding experimental
  Acrylic state. The entry point is resolved dynamically, both effects keep
  `DWMWA_SYSTEMBACKDROP_TYPE` disabled, and switching to Mica, Mica Alt,
  transparent or solid explicitly clears the accent policy.
- Schema 37 retires the second global terminal-opacity layer. The single
  backdrop opacity now colors both the session strip and the transparent
  default terminal background; per-session terminal overrides remain valid.
- The per-surface alpha formulas are removed from `Theme`; surfaces read solid
  colors from the skin.

## Consequences

### Native composition correction (2026-09-29)

Transparent, Glass and Acrylic use a dark native frame substrate independently
of the QML palette. On the tested Windows 11/D3D11 configuration, requesting a
light native frame left an opaque white underlay even when the captured Qt scene
was fully transparent. Keeping the scene light and changing only the native frame
removed that underlay. The custom caption, text and controls remain light; Mica,
Mica Alt, solid and high-contrast paths retain their native-theme behavior.

The opt-in `--material-theme-capture --data-dir <isolated-directory>` check opens
a local test terminal and saves both scene and desktop captures for all six
materials in both palettes. Run with a visible interactive desktop (not hidden
startup). It rejects occluded captures by checking an opaque icon, checks that
transparent output responds to two backing colors, that solid output does not,
and that 100% tint remains opaque. It exits and stops its local child afterward.
This desktop comparison caught what the earlier DWM-attribute-only check missed.
Generated captures stay under the isolated directory, not in source control.

Validation: the original light transparent capture was white against both
backing colors, while its Qt scene had alpha zero. After correction both palettes
respond to backing color changes. The six-material/two-palette captures, 0%/100%
checks, native appearance/state smokes, QML quality and four focused CTest checks
passed. Full static-Release clang-tidy completed with only the already reviewed
Qt `qobjectdefs.h:624` callback findings (three production paths and the dedicated
lifetime probe; see `docs/testing/MEMORY_LEAK_CHECKS.md`), not a zero-warning gate.

- Backdrop opacity affects one shared terminal/title tint, so lowering it can
  no longer make settings text unreadable.
- Controls no longer need special-case tints to stand out from a translucent
  page.
- Existing `backdropOpacity` semantics change: the value now scales one tint
  behind the terminal title and viewport instead of stacking separate chrome
  and terminal-background alpha layers.
