# ADR 0023: System font catalog and terminal ligatures

Status: accepted for V1.3

## Context

Free-form font-family text fields are difficult to discover and easy to mistype.
Terminal users normally want an installed monospaced font, while the application
interface should follow Windows unless the user deliberately overrides it.
Programming ligatures are also font-dependent and cannot be implemented as a
cosmetic switch when the terminal currently draws every cell independently.

## Decision

- A C++ `FontCatalog` owns installed-family discovery, fixed-pitch filtering,
  Windows/Qt system-UI-font resolution, CJK glyph checks, and OpenType ligature
  feature detection. QML receives immutable lists and lightweight query methods;
  it does not enumerate the operating system itself.
- Both interface and terminal font selectors are searchable pickers backed by
  installed families. The terminal picker shows only fixed-pitch families by
  default and offers an explicit, persisted opt-in to show every installed font.
- The interface font is empty in persisted settings when it follows the system.
  A custom family is global. Qt/Windows fallback remains active so mixed-script
  text can use appropriate Chinese glyphs when the primary family lacks them.
- Terminal font and ligature preferences are global rather than per SSH profile.
  Selecting a proportional font is allowed for expert users but produces a clear
  warning because the terminal grid itself remains fixed-width.
- Enabling ligatures allows the painter to shape contiguous compatible ASCII,
  single-width cells as one run. Runs stop at style, foreground, selection,
  display-column, IME, wide-character, and visible-cursor boundaries. Disabling
  the setting explicitly disables the common OpenType ligature features.
- The terminal engine and snapshot remain cell-based. Shaping is a viewport-only
  concern and never changes cursor positions, selection, copying, or remote data.

## Consequences

- Font discovery and feature checks happen once in native code instead of during
  QML delegate creation.
- A font can advertise ligature features yet contain no useful programming
  sequences; the UI describes support as OpenType capability rather than making
  a stronger promise.
- Ligature rendering is deliberately conservative. Wide glyphs, spaces, mixed
  styles, IME composition boundaries, and the active cursor remain individually
  addressable and cannot be consumed by a shaped run.
- Future cross-platform ports can replace system font policy behind the catalog
  without changing the QML picker or settings schema.
