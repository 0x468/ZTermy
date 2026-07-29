# V1 UI/UX convergence plan

Status: in progress

## Goal

V1 should initially feel familiar to a Netcatty user while remaining an
independent Windows 11 application. Netcatty is a reference for workflow,
discoverability, density, and information hierarchy. ztermy owns its QML,
components, icons, text, theme tokens, and native behavior.

Visual work is part of V1, not a post-V1 reskin. The functional shell built
during the terminal and SSH milestones is the prototype that the convergence
pass will refine.

The accepted V1 tokens, component-state contract, accessibility rules, and
performance boundary are defined in [UI_DESIGN_SYSTEM.md](UI_DESIGN_SYSTEM.md).

## Current convergence evidence

- The shell, terminal tabs, host vault, settings, prompts, fields, status
  messages, and recovery states use the shared semantic theme and component
  foundations.
- The real-window responsive-layout gate passes in dynamic Debug and static
  Release. It exercises the minimum `500x360` window and the regular
  `1120x800` window. Hosts and Settings switch between compact single-column
  and regular two-column layouts with positive bounded content widths.
- Shared combo-box, spin-box, slider, switch, and check-box wrappers preserve
  Qt interaction semantics while using the same field, border, focus, icon,
  motion, and disabled-state tokens as buttons and text fields.
- The gate captures ztermy-owned Hosts and Settings screenshots for Dark and
  Light themes at both breakpoints. Review confirmed readable control labels,
  stable page insets, visible title-bar commands after live theme changes, and
  no clipped top-level actions.

## Direction

- Professional, compact, dark-first desktop tooling.
- A quiet neutral surface hierarchy with green reserved for connection,
  readiness, and primary-action emphasis.
- Terminal content remains the visual focus.
- Stable hover states, visible keyboard focus, and no layout-shifting effects.
- Consistent icon family and metrics; no emoji or copied product assets.
- Typography and spacing optimized for desktop density rather than a web
  dashboard or marketing layout.
- Light theme and optional Windows backdrop remain supported through semantic
  tokens instead of page-specific colors.

## Reference study

Before changing a screen, record observations rather than copying
implementation details:

1. Capture the user goal and navigation path in Netcatty.
2. Record information hierarchy, control placement, density, state feedback,
   and keyboard behavior.
3. Compare the same workflow with at least one Windows-native terminal or SSH
   tool when native conventions matter.
4. Translate the observations into ztermy components and semantic tokens.
5. Keep screenshots only as local research evidence; do not ship them or
   derive application assets from them.

The initial screen inventory is:

- window chrome and workspace navigation;
- terminal tab strip and session state;
- local and SSH terminal workspace;
- host vault list, grouping, search, editor, and destructive confirmations;
- host-key confirmation and connection failure states;
- settings navigation and appearance controls;
- search, empty, loading, and offline/closed-session states.

The terminal closed-session state uses the shared `StatePanel` with direct
recovery actions for a new local terminal or the saved-host workspace.

## Implementation stages

### 1. Design-system foundation

- Define semantic color, typography, spacing, radius, border, elevation, icon,
  motion, and focus tokens.
- Replace repeated QML literals with shared tokens and reusable primitives.
- Establish compact and comfortable density metrics without changing terminal
  cell geometry.
- Document component states: default, hover, pressed, focused, disabled,
  selected, warning, error, and connected.

### 2. Shell and navigation convergence

- Align the custom title bar, workspace rail, tab strip, status bar, and main
  content hierarchy.
- Preserve native hit testing, Snap Layouts, resize behavior, and draggable
  regions while visual metrics change.
- Verify 100%, 125%, 150%, and 200% scale factors.

### 3. Primary workflow convergence

- Refine terminal tabs, connection feedback, host vault, profile editing,
  search, host-key confirmation, and error recovery.
- Ensure every action is discoverable by mouse and keyboard.
- Add purposeful empty, loading, disconnected, and validation states.

### 4. Settings and appearance

- Expose supported theme, opacity, backdrop, terminal typography, cursor, and
  behavior settings through the same component system.
- Preview visual changes safely and provide reset-to-default behavior.
- Keep content opacity independent from the optional Windows backdrop.

### 5. V1 visual acceptance

- Compare each inventory screen against the recorded reference observations.
- Verify hierarchy and behavior at normal, narrow, maximized, snapped, and
  mixed-DPI window sizes.
- Verify dark and light contrast, keyboard focus order, reduced motion, IME,
  CJK labels, and text scaling.
- Capture ztermy-owned screenshots for the release record.
- Run terminal latency and large-output checks again after visual changes.

## Acceptance boundary

The convergence pass is complete when the primary workflows share one coherent
component system, meet the Windows 11 runtime checks, and retain terminal
performance. Pixel-perfect Netcatty reproduction, copied branding, and full
feature parity are explicitly outside the boundary.
