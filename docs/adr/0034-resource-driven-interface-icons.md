# ADR 0034: Resource-driven interface icons

Status: accepted

## Context

The first ztermy UI milestones stored every interface icon path in a QML
`switch`. That kept theme coloring simple, but it mixed design assets with
presentation code, made visual review difficult, and allowed unrelated icons
to drift onto different grids and stroke weights. Loading plain SVG files with
`Image` would restore proper source assets but would not resolve
`currentColor`, and using private Qt Quick Controls implementation types or a
live effect layer per icon would add unstable API or rendering overhead.

## Decision

- Store monochrome interface masters under `resources/icons` on a `20 x 20`
  grid with a rounded `1.5` stroke and `currentColor` paint token.
- Keep multicolor product identity assets separately under
  `resources/branding`.
- Expose icons to QML through `AppIcon`, backed by a native
  `QQuickImageProvider`. The provider validates the icon name, resolves
  `currentColor`, and rasterizes with Qt SVG at the requested device-pixel
  size.
- Include icon name, color, and raster dimensions in the image URL so Qt's
  image cache can reuse a rendered result without retaining a live effect layer
  for every button.
- Treat the checked-in SVG files as the production source of truth. The web
  gallery remains a review tool and does not execute in the application.
- Use resource icons for icon-only actions and control indicators instead of
  font glyphs or one-off QML geometry, so stroke weight and alignment remain
  stable across UI fonts, themes, and display scaling.

## Consequences

### 2026-09-29: approved Tabler interface rollout

Use the reviewed Tabler v3.35.0 outline mappings for the entire interface set,
with provenance in `resources/icons/sources.json` and the embedded MIT notice.
Keep ztermy branding untouched and retain existing resource IDs, theme tokens,
image-provider caching, hit targets and native caption behavior. This is an
asset replacement. The subsequent approved effects pass adds a 120 ms hover
fade and a 70 ms press-scale feedback to shared icon buttons only. Scale is
disabled for reduced/off effects; caption buttons keep their original native
behavior. Animate geometry/opacity, not SVG color, to avoid per-frame raster
cache entries. Keyboard focus and selection remain distinct indicators.
The memory and disk hardware silhouettes are explicit locally drawn exceptions;
the upstream desktop-pin icon replaces the window-pinning glyph. All 79 assets
are checked for successful rasterization at small sizes in both theme colors.

#### Rollout verification (2026-09-29)

- Dynamic Release, dynamic Debug, and static Release: all 127 registered CTest
  entries passed. Optional real-host/long-soak cases remain environment-gated;
  this is not a claim that external-host acceptance ran.
- Native `--profile-icons-smoke`, `--profile-card-icons-smoke`,
  `--toolbar-hover-smoke`, `--ui-keyboard-smoke` and
  `--title-navigation-mouse-smoke` exited successfully with isolated data.
  Inspected dark/light picker and actual host/recent-card screenshots. Verified
  Enter/Tab/Escape popup access, focus restoration and stable press hit targets.
- C++ formatting, QML formatting/lint, translation and asset checks passed.
  Renderer coverage includes all 79 icons at 16/20/30/40 raster pixels in both
  light and dark ink. Compare premultiplied edge colors to avoid amplifying
  antialiasing quantization when unpremultiplying.
- Full static-Release clang-tidy visited 317 translation units. Fixed local
  cast/copy diagnostics and generated the excluded image probe's module response
  file before analysis; affected units then passed. Three diagnostics remain in
  Qt 6.8.3 `QtCore/qobjectdefs.h:624`, `NewDeleteLeaks` for `callable`, reached from
  SSH/local presentation and SSH-session queued callbacks. They are reviewed
  third-party false positives, **not a zero-warning analysis result**: that
  version's `QMetaObject::invokeMethodImpl` immediately adopts the allocation
  into `SlotObjUniquePtr`, then transfers it into `QMetaCallEvent` for queued
  dispatch. Receiver destruction removes pending events. No checker disabled,
  dependency patched, or product callback lifetime changed to silence this.
  Source: https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/kernel/qmetaobject.cpp
- Full regression exposed two outdated local-terminal fixtures: CPR was read
  through PowerShell keyboard translation instead of VT input, and Nushell
  startup readiness assumed a fixed welcome-message byte count. The former now
  uses a native child with VT input enabled; the latter waits for its prompt.
  Protocol coordinates and consecutive prompt-row assertions remain intact.

- QML call sites retain the small `AppIcon { name; color }` contract while the
  visual assets become independently reviewable and reusable.
- Theme, accent, disabled, and semantic colors remain dynamic without private
  QML APIs.
- High-DPI windows request an appropriately sized raster from the same SVG
  master.
- New production icons require an SVG resource entry and must pass the asset,
  renderer, QML, and runtime smoke checks.
