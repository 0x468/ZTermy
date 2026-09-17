# UI/UX V2 plan — unified style library

Status: in progress on branch `ui/v2-design-system` (started 2026-09-18)

This branch may break visual and settings compatibility. The terminal
performance boundary in [UI_DESIGN_SYSTEM.md](UI_DESIGN_SYSTEM.md) still
applies: one scene-graph item per terminal viewport, no QML object per cell,
and page transitions must not force terminal snapshots.

## Goal

Replace the scattered per-file styling with one style library so every
surface, control, popup and transition reads from the same tokens. Default to
the high-quality tier (native material, shadows, full motion, warm caches);
keep `reduced` and `off` tiers for low-end machines and the Windows
"reduce animation" preference.

## Principles

1. **Material is a workspace property, not a window property.** Mica/Acrylic
   only shows through the title bar, tab strip and terminal workspace. Content
   pages (Hosts, Settings, AI, SFTP, Notes, logs), popups, menus, toasts and
   dialogs are opaque panels. Hierarchy comes from elevation (shadow +
   hairline), not from stacked alpha.
2. **Two token layers.** A terminal palette (background, foreground, cursor,
   selection, 16 ANSI colors) and a chrome skin (surface ladder, ink ladder,
   accent family, semantic status colors, hairline, veil). The skin can derive
   from the palette or override it. Both are theme data, not QML constants.
3. **Motion by role, not by speed.** `Motion` exposes roles (enter, exit,
   relocate, fade, emphasis, page) with duration + easing; QML uses shared
   transition components rather than ad-hoc `NumberAnimation`s.
4. **Elevation is a component.** `AppSurface { elevation: 2 }` owns radius,
   fill, hairline and shadow; nothing else draws its own shadow.
5. **Effects tier is a single switch.** `Theme.effectsTier` ∈ full | reduced |
   off; it gates material, shadows, blur-like effects and motion in one place.

## Chapters

| # | Chapter | Deliverable | Status |
|---|---------|-------------|--------|
| 1 | Material scope + effects tier | Opaque content pages/popups, material limited to chrome + workspace, `effectsTier` token, ADR 0120 | ☑ 2026-09-18 |
| 2 | Elevation library | `AppSurface` elevation 0–3 with built-in shadow, radius tokens, hard-coded radii removed | ☑ 2026-09-18 |
| 3 | Motion library | `Motion` singleton, shared transitions for page/tab/pane/menu/toast, respects tier and Windows animation preference | ☑ 2026-09-18 |
| 4 | Theme library + picker | `Theme` split into palette/skin, built-in themes, JSON custom themes, Windows Terminal/Ghostty import, picker dialog with hover preview, ADR 0121 | ☐ |
| 5 | Settings reorganisation | Grouped navigation, search, per-row reset, appearance page rebuilt around the theme strip | ☐ |
| 6 | Chrome polish | Tab strip, caption buttons, pane headers, drag previews on the new library | ☐ |

Each chapter lands as small Conventional Commits with tests and the existing
runtime smokes passing; a before/after screenshot pair per chapter goes under
`docs/design/ui-v2/`.

## Evidence

- Effects tier `full` must keep the terminal benchmark within the numbers
  recorded in `CHANGELOG.md` (Performance pass — 2026-09-18).
- Each chapter records the ctest suites and runtime smokes run.

## Progress log

- 2026-09-18: branch created, plan and ADR 0120 written.
- 2026-09-18: Chapter 1 landed. `effectsTier` persisted (settings schema 34,
  `AppController.effectsTier`/`saveEffectsTier`), `Theme.effectsTier` gates
  material (`effectiveBackdrop`), shadows and motion; the per-surface alpha
  ladder is gone and only `chromeBackground`/`workspaceBackground` keep the
  material tint. Settings gains a "Visual effects" row; the backdrop combo is
  disabled outside `full`. Evidence: ctest `application-settings`,
  `app-controller`, `workspace-state-store`; `--ui-layout-smoke` and
  `--pane-scrollbar-smoke` exit 0; code health gate PASS; captures in
  `docs/design/ui-v2/ch1/` (the "before" state is reproducible from `main`
  at 713ee66 with `--ui-layout-smoke`).
- 2026-09-18: Chapter 2 landed. `AppSurface` (elevation 0 panel, 1 card,
  2 floating, 3 dialog) owns fill, hairline, radius and a `MultiEffect`
  shadow gated by `Theme.shadowsEnabled`; SectionCard, SidePanelSurface,
  StatePanel, AppMenu, AppToolTip, combo/suggestion/font popups, toasts,
  transfer center, terminal popovers, ConfirmationDialog, CommandPalette,
  Main.qml dialogs, search panel and drag previews all sit on it.
  `Theme.radiusCompact` and shadow tokens added; no numeric `radius:` remains
  in QML (dots and pills use `height / 2`). `QtQuick.Effects` is a module
  dependency; the portable and MSI verifications require
  `Qt6QuickEffects.dll` and `effectsplugin.dll`. The palette/skin split moves
  to Chapter 4 where the theme data model is introduced. Evidence:
  `ztermy_qml_format_check`, `ztermy_qmllint`, `ztermy_dynamic_deploy`;
  `--ui-layout-smoke` and `--pane-scrollbar-smoke` exit 0; code health gate
  PASS; captures in `docs/design/ui-v2/ch2/`.
- 2026-09-18: Chapter 3 landed. `Motion` singleton exposes roles (feedback,
  enter, exit, relocate, page, emphasis) with durations, easings, travel
  distance and reveal scale, all scaled by the effects tier and zeroed by
  `off`/Windows animation preference. `MotionColor`, `MotionFeedback`,
  `MotionRelocate`, `MotionEnter`, `MotionExit` and `MotionReveal` replace
  every ad-hoc `NumberAnimation`/`ColorAnimation`/`Transition`; the old
  `Theme.motion*` tokens and the per-site `animationsEnabled ? x : 0` guards
  are gone. Evidence: `ztermy_qml_format_check`, `ztermy_qmllint`;
  `--ui-layout-smoke` and `--pane-scrollbar-smoke` exit 0; code health gate
  PASS.
