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

## Consequences

- QML call sites retain the small `AppIcon { name; color }` contract while the
  visual assets become independently reviewable and reusable.
- Theme, accent, disabled, and semantic colors remain dynamic without private
  QML APIs.
- High-DPI windows request an appropriately sized raster from the same SVG
  master.
- New production icons require an SVG resource entry and must pass the asset,
  renderer, QML, and runtime smoke checks.
