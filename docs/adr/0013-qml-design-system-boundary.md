# ADR 0013: QML design-system boundary

Status: accepted

## Context

The functional V1 screens already share a broad dark-first direction, but
several pages still own literal colors, font names, spacing, dialog styling,
and control-state behavior. Continuing that pattern would make light theme,
keyboard focus, reduced motion, density, and later visual refinement diverge
screen by screen.

Generic design-system recommendations aimed at websites or mobile
applications are not automatically suitable for a compact native terminal.
ztermy also must preserve Win32 title-bar hit testing and keep terminal cells
out of the QML object tree.

## Decision

`Theme.qml` is the runtime source of semantic visual tokens. Reusable QML
components own application-control interaction states, focus presentation,
accessibility metadata, and metrics. Pages own workflow composition and
lightweight binding only.

Qt Quick Controls remain the implementation for complex standard controls
such as editable text fields, combo boxes, sliders, and spin boxes. Project
wrappers normalize their palette, metrics, validation presentation, and
accessibility instead of recreating their input behavior.

Native title-bar behavior stays behind `NativeWindow`; visual component
refactoring must not replace or emulate Win32 hit testing. Terminal rendering
remains one custom item and is explicitly outside the application-component
system.

The accepted V1 visual direction and component-state contract are recorded in
`docs/UI_DESIGN_SYSTEM.md`. Green is a semantic accent rather than decorative
neon. Layout-changing hover effects, downloaded web fonts, and presentation
page patterns are excluded.

## Consequences

- New screens cannot introduce page-specific palettes for ordinary controls.
- Existing screens migrate incrementally without rewriting standard text
  input behavior.
- Dark/light contrast, focus, density, and reduced-motion fixes can be applied
  consistently.
- UI convergence still requires runtime checks at multiple scales because QML
  loading and unit tests cannot prove visual hierarchy or native behavior.
- Visual refactors must rerun terminal latency, IME, resize, and Snap Layout
  acceptance checks.
