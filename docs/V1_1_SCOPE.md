# ztermy V1.1 scope

Status: accepted on 2026-08-01

## Goal

V1.1 is a focused UI/UX convergence release. NetCatty remains the primary
product reference for workflow hierarchy, density, discoverability,
interaction feedback, and motion, while ztermy keeps its own native C++/Qt
implementation, Windows behavior, visual identity, text, icons, and assets.

The release should make the relationship to the reference direction obvious
without copying or claiming pixel-perfect compatibility.

## Priorities

1. Refine the top-level work-tab chrome and terminal workspace.
2. Turn Hosts into a browse-and-connect-first dashboard.
3. Align page hierarchy, spacing, control density, and secondary actions.
4. Establish purposeful page, tab, dialog, and state transitions.
5. Preserve complete keyboard operation, reduced motion, IME, Snap Layouts,
   DPI behavior, and terminal latency.
6. Complete a screenshot-backed dark/light and normal/narrow acceptance pass.

## Product mapping

| NetCatty reference surface | ztermy V1.1 treatment |
| --- | --- |
| Vaults root tab | Hosts root tab |
| Session work tabs | Local and SSH terminal tabs |
| Host search and quick connect | Host search and connect-first dashboard |
| Recent-host cards | Recent connections derived from local profile state |
| Host cards and groups | Saved SSH profiles and existing groups |
| Host editor | Existing SSH profile editor, visually converged |
| Terminal top information row | Connection identity, state, search, and terminal actions |
| Settings categories | Only categories supported by ztermy |
| Dialog and recovery states | ztermy security and session states using the shared component system |

Surfaces for SFTP, serial, Mosh, Telnet, port forwarding, AI, cloud sync,
scripts, notes, and other unsupported features are not recreated as empty or
disabled navigation. They may be considered by later feature milestones on
their own merits.

## Explicit serial exclusion

V1.1 does not implement serial connections and must not expose:

- a Serial or COM-port action beside the terminal action;
- a serial protocol option in the host editor;
- serial-port discovery, baud-rate, data-bit, parity, stop-bit, or flow-control
  fields;
- YMODEM or serial transfer commands;
- placeholder serial tabs, cards, menus, icons, or settings.

No serial runtime or dependency is added merely to match the reference UI.

## Visual direction

- Compact Windows desktop tooling rather than a web dashboard.
- Dark-first neutral surfaces, with full Light and System support.
- Segoe UI Variable remains the application font; terminal typography remains
  independently configurable.
- ztermy green remains the default semantic accent. Reference colors and
  branding are not copied.
- The global accent source may instead follow the live Windows accent or use
  a validated custom `#RRGGBB` color. SSH profiles do not own accent settings.
- The terminal owns the largest, quietest surface.
- Primary actions are obvious; edit, copy, delete, and advanced operations are
  progressively disclosed.
- Borders, surface steps, typography, and spacing establish hierarchy before
  shadows or decoration.

## Motion direction

Motion communicates state or spatial relationship. It never delays terminal
input or animates terminal cells.

- Hover and press feedback: 100-140 ms.
- Tab and compact content entry: 160-220 ms.
- Panel, dialog, and page transitions: 180-240 ms.
- Theme interpolation: at most 220 ms.
- Preferred easing: an ease-out or standard emphasized curve without bounce.
- No scale-on-hover, parallax, cursor trails, decorative glow loops, or layout
  movement under a stationary pointer.
- Windows reduced-motion/client-animation settings reduce nonessential motion
  to an immediate state change.

## Delivery stages

### 1. Reference baseline and design contract

- Record the page and interaction mapping in
  [V1_1_UI_UX_AUDIT.md](V1_1_UI_UX_AUDIT.md).
- Extend semantic geometry and motion tokens.
- Establish screenshot and interaction acceptance cases before visual changes.

### 2. Work-tab chrome and terminal workspace

- Refine work-tab sizing, active state, session status, close behavior, new-tab
  placement, overflow, and entry/exit feedback.
- Refine the terminal information row and secondary terminal actions.
- Preserve the native title-bar hit-test and Snap Layout contract.

### 3. Hosts dashboard

- Recompose Hosts around search, quick connect, recent connections, groups,
  and saved-host cards.
- Make connecting primary and move record management into contextual or
  progressively disclosed actions.
- Preserve profile validation, destructive confirmation, and keyboard routes.

### 4. Settings, editors, and transient surfaces

- Converge settings hierarchy and density without displaying unsupported
  categories.
- Open Settings as a singleton, on-demand work tab from the global title-bar
  shortcut. Inside the tab, use a left category rail and right detail page.
- Refine host editing, host-key confirmation, paste confirmation, empty,
  loading, disconnected, and error recovery states.

### 5. Interaction and motion convergence

- Apply the shared motion tokens to tab lifecycle, page changes, dialogs,
  popups, status changes, and responsive transitions.
- Verify focus restoration, no pointer-only actions, and no motion-induced
  layout instability.

### 6. V1.1 acceptance

- Run automated formatting, linting, static analysis, unit tests, real-window
  layout/DPI gates, terminal latency, and distribution checks.
- Complete manual dark/light/System, normal/narrow/maximized/snapped,
  keyboard-only, IME/CJK, native-window, and sustained-terminal checks.
- Retain ztermy-owned screenshots for the final record.

## Non-goals

- Copying NetCatty source, CSS, fonts, icons, themes, screenshots, wording, or
  branding.
- Pixel-perfect compatibility.
- Full NetCatty feature parity.
- Serial support.
- Adding unrelated back-end features to make the navigation look fuller.
- Replacing the native window or terminal renderer with Electron/web content.

## Major decisions

The owner selected an on-demand, singleton Settings work tab in the main
window. A global title-bar shortcut opens it; its content follows the
reference category-rail/detail-page pattern. See
[ADR 0018](adr/0018-on-demand-settings-work-tab.md).

Further decisions are added here when they materially change navigation,
security boundaries, persistence, terminal performance, or the Windows-native
contract.
