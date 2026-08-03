# ztermy brand baseline

Status: approved on 2026-08-03.

## Chosen mark

The ztermy application mark is **Ribbon Z · Inset Prompt**: a blue folded-ribbon `Z` on a dark navy rounded tile, with a deliberately quiet `>_` terminal prompt inset into the lower middle stroke.

The `Z` remains the primary silhouette. The prompt is a secondary recognition detail and must never compete with it.

## Master assets

- `resources/branding/ztermy-app-icon.svg` — full application icon for 24 px and larger contexts.
- `resources/branding/ztermy-app-icon-small.svg` — simplified icon without the prompt for 16 px and 20 px contexts.
- `resources/branding/ztermy-lockup-horizontal.svg` — two-line main-page lockup on a transparent canvas.
- `resources/branding/ztermy-lockup-about.svg` — three-line about-page brand card.

SVG is the editable source of truth. Windows `.ico` and raster exports must be generated from these masters rather than edited independently.

Build `ztermy_branding_assets` to create the derived files under the selected CMake preset's `generated/` directory. See `resources/README.md` for the source/output boundary.

## Color tokens

| Role | Value |
| --- | --- |
| Tile | `#102544` |
| Ribbon highlight | `#7DE7FF` |
| Ribbon base | `#3C89FF` |
| Prompt | `#E9F7FF` at 100% opacity |
| Wordmark `Z` | `#2AA8FF` |
| Light wordmark | `#0F172A` |
| Descriptor | `#64748B` |

## Geometry

- The icon is authored on a `20 × 20` design grid.
- The tile occupies `(1, 1)` through `(19, 19)` with a `4.2` radius.
- The ribbon path's geometric bounding-box center is `(10, 10)`; keep it aligned with the tile center.
- Keep the transparent outer margin; do not crop the rounded tile to the artboard edge.
- The inset prompt is intentionally shifted `0.3` grid units below the ribbon's geometric center. Do not move or enlarge it without a new optical review. Its full-opacity stroke makes the terminal identity explicit while its smaller size preserves the ribbon silhouette.
- Preserve the original aspect ratio. Do not skew, stretch, add shadows, or recolor individual ribbon segments.

## Size policy

| Rendered size | Asset | Prompt treatment |
| --- | --- | --- |
| 16–20 px | `ztermy-app-icon-small.svg` | Omitted |
| 24 px | Full icon if legible; otherwise small icon | Optional |
| 32 px and larger | `ztermy-app-icon.svg` | Included |

The Windows icon pipeline exports `16, 20, 24, 32, 40, 48, 64, 128, 256` px and selects the simplified master for the two smallest layers.

## Lockup alignment

- Main-page lockup: the right-side block is optically centered on the large `Ztermy` wordmark; `SSH TERMINAL` sits below it as a subordinate line.
- About-page lockup: center the complete three-line group (`Ztermy`, descriptor, supporting line) as one unit. Do not reuse the two-line main-page vertical offset.
- Title-bar use: omit the descriptor and supporting line. Prefer the standalone app icon when horizontal space is constrained.
- The in-app title-bar variant uses `accent` for the tile, `accentText` for the ribbon, and the opposite black/white contrast color for the prompt. At 24 px the prompt stroke is optically increased to approximately one physical pixel so the terminal identity remains visible.
- The current wordmark uses the Windows system font stack (`Segoe UI Variable`, `Segoe UI`, `Arial`) without embedding font data, consistent with the Windows 11-first product scope.

## Usage boundary

This is original ztermy branding. Reference applications and third-party icon libraries may inform interaction patterns, but their artwork, trademarks, and brand assets must not be copied into this set.
