# ADR 0031: Follow the Windows high-contrast contract

## Status

Accepted

## Context

ztermy already follows the Windows light/dark preference, accent color, and
client-area animation preference. Windows high-contrast themes are a separate
accessibility mode: translucent materials and product-owned palette colors can
make content unreadable even when their ordinary contrast ratios are sound.

## Decision

- Query `SPI_GETHIGHCONTRAST` and the current Windows system colors behind the
  Windows platform boundary.
- Refresh that state on `WM_SETTINGCHANGE` and expose it to QML through
  `NativeWindow`.
- While high contrast is enabled, use the Windows window, text, highlight, and
  highlight-text colors; make application surfaces opaque; disable Acrylic,
  Transparent, Mica, and Mica Alt rendering; and disable non-essential motion.
- Keep the saved appearance preference unchanged. When high contrast is turned
  off, the user's selected material, opacity, accent, and motion preference
  resume without a migration or destructive rewrite.

## Consequences

- The accessible palette follows the user's Windows theme instead of assuming a
  black high-contrast theme.
- Visual hierarchy relies more heavily on system-colored borders and focus
  indicators while high contrast is active.
- Actual high-contrast switching remains a real-window release check because it
  depends on Windows state and DWM composition; the preference state machine and
  color decoding remain unit-testable.

