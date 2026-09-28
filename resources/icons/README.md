# Interface icons

These monochrome SVG files are the production masters for ztermy interface
icons. They are separate from the multicolor product identity in
`resources/branding`.

The approved interface set uses Tabler Icons v3.35.0 (MIT). `sources.json`
maps stable ztermy resource IDs to upstream files. `memory` and `disk` remain
ztermy-drawn hardware silhouettes with the same stroke conventions, because
this pinned upstream version has no matching DIMM/hard-drive outline.
The complete license is embedded from `resources/third-party/Tabler-Icons-NOTICE.txt`.
Do not rename IDs as part of visual replacement: Profiles and QML reference them.

- Canvas: `20 x 20`
- Default stroke: `1.5`
- Caps and joins: rounded
- Paint token: `currentColor`
- Safe runtime sizes: `16`, `20`, `24`, and `32` device-independent pixels

QML uses `AppIcon` instead of loading these files directly. The native image
provider resolves `currentColor`, renders at the active device-pixel ratio, and
lets Qt cache the resulting image by icon name, color, and raster size.
