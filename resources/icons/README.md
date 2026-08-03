# Interface icons

These monochrome SVG files are the production masters for ztermy interface
icons. They are separate from the multicolor product identity in
`resources/branding`.

- Canvas: `20 x 20`
- Default stroke: `1.5`
- Caps and joins: rounded
- Paint token: `currentColor`
- Safe runtime sizes: `16`, `20`, `24`, and `32` device-independent pixels

QML uses `AppIcon` instead of loading these files directly. The native image
provider resolves `currentColor`, renders at the active device-pixel ratio, and
lets Qt cache the resulting image by icon name, color, and raster size.
