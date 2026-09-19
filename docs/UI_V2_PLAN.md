# UI/UX V2 plan — unified style library

Status: chapters 1–6 landed on branch `ui/v2-design-system`
(2026-09-18 → 2026-09-19); delivery verification is recorded in the progress
log and in [testing/UI_V2_DELIVERY.md](testing/UI_V2_DELIVERY.md). Not merged.

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
| 4 | Theme library + picker | `Theme` split into palette/skin, built-in themes, JSON custom themes, Windows Terminal/Ghostty import, picker dialog with hover preview, ADR 0121 | ☑ 2026-09-18 |
| 5 | Settings reorganisation | Grouped navigation, search, per-row reset, appearance page rebuilt around the theme strip, ADR 0122 | ☑ 2026-09-18 |
| 6 | Chrome polish | Tab strip, caption buttons, pane headers, drag previews on the new library, ADR 0123 | ☑ 2026-09-19 |

Each chapter lands as small Conventional Commits with tests and the existing
runtime smokes passing; a before/after screenshot pair per chapter goes under
`docs/design/ui-v2/`.

## Evidence

- Effects tier `full` must keep the terminal benchmark within the numbers
  recorded in `CHANGELOG.md` (Performance pass — 2026-09-18).
- Each chapter records the ctest suites and runtime smokes run.
- Delivery benchmark (2026-09-19): same-day `main` (713ee66) and the branch,
  both Release, acrylic, 1120×800, DPR 1, 24 warm runs each in four batches
  (three interleaved, one serial per tree) plus a seeded effects-tier A/B.
  Raw runs and the median script live under `build/perf-evidence/ui-v2/`
  (not committed); the numbers are in the 2026-09-19 progress entry.

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
- 2026-09-18: Chapter 4 landed. `TerminalColorScheme` + `TerminalEngine::setColorScheme`
  (Ghostty palette/fg/bg/cursor options), sessions queue `ColorSchemeCommand`
  and reapply on restart; `TerminalThemeCatalog` with ten built-ins, JSON
  custom themes in `<data>/themes`, Windows Terminal and Ghostty importers;
  settings schema 35 `terminalTheme`; `AppController` theme properties,
  preview, import and remove (`AppControllerThemes.cpp`); `Theme` palette
  layer bound from the controller, workspace fill and selection colors derive
  from it, "ztermy" accent follows the theme's accent hint; `TerminalItem`
  selection colors are properties; `ThemePickerDialog` (elevation 3, hover
  preview, import/remove) opened from the `TerminalThemeStrip` in Settings.
  Evidence: ctest `terminal-engine`, `terminal-theme-catalog`,
  `application-settings`, `app-controller`; `ztermy_qml_format_check`,
  `ztermy_qmllint`; `--ui-layout-smoke` and `--pane-scrollbar-smoke` exit 0;
  code health gate PASS (baseline ratcheted); captures in
  `docs/design/ui-v2/ch4/`. ADR 0121.
- 2026-09-18: Chapter 5 landed. `SettingsCategoryRail` groups the categories
  (General, Connections, Assistant, About) with a search field whose index
  maps row keys to categories; Enter or a result opens the category, scrolls
  to the row and pulses its highlight. `SettingsRowLabel` carries the caption
  plus a reset affordance that appears when the draft differs from the
  default; defaults come from `AppController::applicationSettingsDefaults`
  (`AppControllerDefaults.cpp`), so QML never repeats a default value. The
  Window appearance card now opens with the terminal theme strip.
  `settingsEffectsTier` joined the appearance Tab order. Keyboard smoke
  helpers (`sendKey`, `sendText`, `focusItem`, `processWindowEventsUntil`)
  moved to `RuntimeSmokeItems.h`; the keyboard smoke covers search -> jump
  -> per-row reset and waits for the overflow tab strip to settle instead of
  a fixed delay. Evidence: ctest `app-controller`; `ztermy_qml_format_check`,
  `ztermy_qmllint`; `--ui-keyboard-smoke`, `--ui-layout-smoke` and
  `--pane-scrollbar-smoke` exit 0; code health gate PASS (baseline
  ratcheted); captures in `docs/design/ui-v2/ch5/`. ADR 0122.
- 2026-09-19: Chapter 6 landed. `TitleTab` (inset hover pill, opaque
  `tabSelectedBackground` card, focus ring; `iconSlot` hosts the brand mark)
  and `TitleChromeAction` (bar-height hover, `menuOpen`, `focusTarget`)
  replace `TitlePageAction`, `TitleTabNavigationAction` and the per-file
  title-bar rectangles; `TerminalTabAction`, `TitleTabOverflow` and every
  quick action in `TitleWindowActions` sit on them. `CaptionButton` draws
  `window-minimize/maximize/restore/close` icons instead of Canvas paths.
  `SessionStatusDot` is shared by tabs, the overflow menu and the new
  `TerminalPaneHeader` (inside the pane frame, hairline divider,
  `paneId`/`paneTitle`/`dragAreaWidth` for the `Main.qml` drag capture,
  `startSystemMove` in detached windows). `DragPreview` and
  `DropTargetIndicator` replace three drag ghosts and two drop highlights;
  the dead QML `DropArea`/`managedPaneDrag` path is gone. The
  `--window-appearance-smoke` now asserts the ADR 0120 contract (chrome and
  workspace alpha follow the material, content/panel/elevated/control/field
  stay opaque) instead of the pre-V2 whole-window alpha ladder it still
  encoded. Evidence: ctest `app-controller`; `ztermy_qml_format_check`,
  `ztermy_qmllint`; `--ui-keyboard-smoke`, `--ui-layout-smoke`,
  `--title-navigation-mouse-smoke`, `--terminal-render-smoke`,
  `--pane-scrollbar-smoke`, `--lifecycle-runtime-smoke` and
  `--window-appearance-smoke` exit 0; code health gate PASS (baseline
  ratcheted); captures in `docs/design/ui-v2/ch6/`. ADR 0123.
- 2026-09-19: Delivery verification. `saveApplicationSettings` rebuilt the
  settings struct from its parameters and never copied `effectsTier` or
  `terminalTheme`, so every Settings Apply reset the terminal theme (and the
  smoke/benchmark theme seed reset the tier); it now starts from the stored
  settings and overwrites only the fields the page edits, with an
  `app-controller` regression. The terminal render smoke then failed on
  Release in about one run in ten: `clickMouse` spun the event loop between
  the synthetic press and release, and when the detached window had just been
  presented from minimized Windows posted `WM_MOUSELEAVE` in that gap, Qt
  cleared the MouseArea hover and the release no longer counted as a click,
  so the caption restore never ran (`qt.qpa.events` shows "Leaving window"
  between the two events in every failing run; product code was not
  involved). Press and release are now delivered back to back; 12/12 Release
  runs pass. Full Debug and Release ctest 129/129,
  `ztermy_dynamic_deploy_smoke` passed, the six runtime smokes exit 0 on the
  rebuilt Release binary, code health gate PASS (AppController.cpp ratcheted
  to 17199). Benchmark medians
  vs same-day `main` 713ee66 (24 runs each): completion 1580 → 1678 ms,
  heartbeat gap 18.5 → 18 ms, paint P50 2 → 2 ms, paint P95 2 → 2 ms (the
  4 ms bucket appears in 11/24 branch runs vs 4/24), paint max 7.15 → 7.22 ms,
  uploaded 405.5 → 425.3 MB, snapshot updates 145 → 151.5, frame swaps
  301 → 302. Completion is quantised by the 100 ms marker search: main lands
  in the 1570–1595 ms tick in 16/24 runs, the branch in 10/24, so the median
  shift is one search tick, not a slower terminal path (P50/max unchanged).
  Tier `off` on the same build measures 1582 ms against 1684 ms for `full`
  (5 seeded runs each, 406.6 vs 429.5 MB uploaded, 279 vs 300 frame swaps),
  so the residual cost sits in the tier-gated chrome (material, shadows,
  motion). Today's `main` does not reproduce the CHANGELOG 2026-09-18 numbers
  either (1471 ms / 366.5 MB there vs 1580 ms / 405.5 MB now), so the
  Evidence rule is read against same-day `main`. Follow-up before merge:
  profile the `full` chrome during the burst (title-bar colour behaviours,
  page reveal) and decide whether `reduced` should be the default on
  battery.
- 2026-09-19: Quality gates. The branch had not run `ztermy_clang_tidy_check`
  (303 translation units, `--warnings-as-errors=*`); it stopped on four
  new-code diagnostics: `modernize-use-designated-initializers` on the ten
  built-in `BuiltInSpec` entries and on the smoke's `SurfaceAlphas`,
  `bugprone-implicit-widening-of-multiplication-result` on the icon cache
  budget, and `bugprone-narrowing-conversions` on the `qreal` → `float`
  slider alpha helper. All four are fixed without behaviour change (the
  theme values were diffed token by token); `SurfaceAlphas` now lives in
  `WindowStateRuntimeSmoke.h` and main.cpp ratchets 4778 → 4739 lines. The
  next passes stopped in the new tests: an `int` from `qsizetype`
  (`bugprone-narrowing-conversions`) in `app_controller_tests.cpp` and
  `bugprone-unchecked-optional-access` on two `catalog.find()` results in
  `terminal_theme_catalog_tests.cpp`, both fixed the way the existing tests
  do it (`static_cast`, `value_or` after `QVERIFY(has_value())`).
  `ztermy_qml_quality_check` also reported an unused `QtQuick.Controls`
  import in `TitleTabOverflow.qml`, removed. The `startCopyMode`
  missing-property qmllint warning predates the branch (same on `main`).
  Re-verified on the rebuilt binaries: `ztermy_format_check`,
  `ztermy_qml_quality_check`, full clang-tidy, Debug and Release ctest
  129/129, `ztermy_dynamic_deploy_smoke`, the six runtime smokes and the
  code health gate all pass.
- 2026-09-19: Owner review round 1 (five findings, all fixed on the branch,
  commits b9d8561..ccb7c68). (1) The session strip, pane toolbar, pane
  header, telemetry strip and pane scrollbar coloured their ink from the app
  skin while sitting on the terminal palette fill, so a dark skin with a
  light terminal theme left the strip unreadable; `Theme` gains a workspace
  ink family (`workspaceText/Soft/Muted/Subtle`, `workspaceBorder`,
  `workspaceControlHover/Pressed`, `workspacePanel/RaisedBackground`) that
  follows the terminal palette's darkness and `AppIconButton.onWorkspace`
  selects it. (2) Settings rail titles and the V2 strings showed in
  English: qsTr contexts are per file, so strings moved into
  `SettingsCategoryRail` lost their `SettingsPane` entries, and lupdate had
  never been run for the new strings; catalog regenerated (91 added, 14
  dead removed, 0 unfinished) and `verify_translations.ps1` now runs
  lupdate against the sources and fails on any string missing from the
  catalog. (3) New tabs created from a content page stayed behind the
  previous tab: the page switch focused the old viewport, whose focus
  change re-activated the old pane; the tab is now created before the
  page switch, and the title-navigation smoke asserts
  `activeTerminalTabId`. (4) No resize cursor over grips: the full-window
  pane drag capture `MouseArea` was disabled, but Qt's cursor lookup only
  skips invisible items and a `MouseArea` always owns an arrow cursor; it
  is now hidden instead, and the resize smoke asserts the window cursor
  over the navigation grip and both split handles (moving the window
  away from the physical pointer first, since Qt re-delivers hover at
  the real pointer each frame). (5) Single pane: the accent frame, radius
  and viewport inset are gone and the session strip loses its hairline so
  strip and terminal are one surface; multi-pane: the accent moves to the
  `AppSplitView` divider next to the active pane (`emphasized`), Windows
  Terminal style, so nothing overlaps the native window edge. Evidence:
  Release ctest 129/129, `ztermy_format_check`, `ztermy_qml_quality_check`,
  full clang-tidy, code health gate PASS, the eight runtime smokes
  (resize-interactions, title-navigation-mouse, ui-layout, ui-keyboard,
  terminal-render, lifecycle, window-appearance, pane-scrollbar) exit 0
  (resize 12/12 after the pointer fix); captures in
  `docs/design/ui-v2/review-1/`. Not merged.
