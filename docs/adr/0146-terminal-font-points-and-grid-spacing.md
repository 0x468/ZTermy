# ADR 0146: Terminal typography uses points with independent grid spacing

- Status: Accepted
- Date: 2026-10-19

## Decision

- User-visible terminal size is in points, defaults to 12 pt, and accepts
  fractional values. Editors step by 0.5 pt without rounding existing values.
  Qt receives the point size; physical display scaling is applied by Qt Quick,
  not multiplied into the font a second time.
- Application settings schema 46 interprets `terminalFontSize` as points and
  adds `terminalFontWeight`, `terminalLineHeight`, and `terminalCellWidth`.
  Schemas 1–45 retain their integer logical-pixel contract. Their size is
  divided by 96/72 exactly once, preserving 15 px as 11.25 pt, for example.
  Loading never rewrites the file; the next explicit save writes schema 46.
  Unrelated preferences and opaque credential references are preserved.
- Weight defaults to 400. SGR bold uses at least 700 and never makes a heavier
  configured font lighter. Normal cells and the block cursor retain the base
  weight; style caches are invalidated on all typography changes.
- Both spacing multipliers default to 1.0, meaning the font's natural cell
  dimensions. These are not Windows Terminal's em-relative `cellHeight` and
  `cellWidth` numbers. Width changes character advance, not glyph stretch;
  extra row space is shared above and below the text. Defaults retain the old
  grid geometry and baseline. Selection, cursor, IME, image placement, hit
  testing, and resize reports use the same adjusted grid metrics.
- Font size is bounded to 6–48 pt, weight to the nine 100–900 steps, line height
  to 1–3 times the natural row, and width to 0.5–3 times the natural advance.
  Store and controller boundaries reject non-finite and malformed values.
- The preview uses an immutable four-row sample in the existing custom terminal
  renderer. It creates no terminal process, connection, or per-cell QML tree.
  Preview changes remain local drafts until Apply; Discard reloads saved values.
- Global settings and tab-local overrides share units and rendering behavior.
  Overrides are transient and reset to the current global settings on request.

## Verification

Focused migration tests cover old even/odd pixel sizes, non-typography field
preservation, save/reload without repeated conversion, and invalid new fields.
Renderer tests compare old pixel and migrated point grid metrics. The real-window
runtime check covers fractional editing, preview isolation, Apply/Discard,
persistence, live-pane bindings, overrides, themes, and responsive layouts.
