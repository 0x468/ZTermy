# Chrome feedback verification — 2026-10-01

Baseline commit: `6e6a262` (signed, not pushed). This follow-up does not change
persisted schemas or restore multi-Tab detached windows.

## Repairs

- The revealed Main title bar previously painted a background but accepted
  input only on its controls. Its passive blank-space drag handler did not
  shield covered controls. A background MouseArea now consumes pointer/wheel
  events and starts native window movement; child buttons retain precedence.
  The shield remains present throughout dismissal.
- Enter/exit use shared Motion fades, without changing terminal geometry or
  native hit coordinates. The active indicator uses the available Tab width
  instead of the previous fixed 24 px cap. The motion/input contract is now
  recorded in AGENTS.md, the design system, and ADR 0135.
- Detached theme tint was assigned to Window.color, which native backdrop
  configuration overwrites. Both window types now paint exactly one identical
  QML workspace tint. Appearance preview also reaches detached native windows.
- The detached Pane toolbar transfer button previously invoked detach again.
  It now returns the complete workspace to Main, preserves its Pane layout,
  and uses the existing localized return label.

## Evidence

- Debug incremental build, C++ formatting, 96-file QML format/lint and the
  2365-string translation gate pass. Focused main.cpp clang-tidy covers the
  inline runtime fixtures. No unrelated full regression was run.
- `title-bar-immersive-runtime`: PASS, 45.36 s. Checks live pointer-held Tab
  switching, no terminal geometry/grid changes, input shielding with a hidden
  negative control, hover exclusion, intermediate enter/exit frames, dismissal shielding,
  restored input, wide/narrow indicators and palette/material captures.
- `window-hit-test` and `window-state`: 2/2 PASS.
- Detached runtime, isolated directory
  `build/test-data/chrome-fixes-final-20261001e`: exit 0. Six dark/light ×
  transparent/acrylic/solid scene comparisons match, including alpha; creation,
  incoming merge, hidden native caption rejection and full-layout return pass.
- Windows MCP review in
  `build/test-data/chrome-fixes-title-review-20261001c`: exit 0. Reveal, multiple
  Tab switches and dragging the unused left navigation edge were performed
  using real mouse input. The window moved from (100,100) to (200,200), rather
  than activating the covered surface.
- Controlled gray-background native activation captures are in
  `build/test-data/chrome-fixes-material-review-20261001d/`:
  `native-main-active.png` and `native-detached-active.png`. Foreground handle
  and Qt active state were checked before saving. Main-active samples are
  RGB(150,150,150) / RGB(152,152,152); detached-active samples reverse those
  values. The small native activation tint is symmetric, not a detached-only
  dark layer. Scene alpha at the chosen adjustable opacity is 115/255 in both.

Early runs are retained, not counted as passes: concurrent pointer-sensitive
fixtures interfered; reused saved Tabs contaminated width checks; and the
initial material comparison sampled the split divider and the opaque Hosts
page. The long desktop review also initially left temporary window geometry
behind; its geometry/Z-order cleanup now passes the short review plus complete
detached lifecycle in the final directory above. Fixtures compare terminal
interiors and reset their isolated Tabs;
pointer runs were repeated serially with only the owned fixture temporarily
topmost. Existing user terminals were not stopped or modified.

## Remaining scope

Detached auto-hide window chrome is a design proposal awaiting approval, not
implemented Tab management. AI sidebar long/heredoc tool-card overlap and
possible execution/completion problems are recorded separately in
`docs/V3_AI_PROGRAM.md`. Real SSH, cross-monitor and high-DPI acceptance are
not inferred from these local, single-display checks.

## Terminal information strip window drag

The expanding status Text previously owned the apparent empty space. It now
shares its layout with a blank-only MouseArea; the alternative blank spacer
uses the same behavior when status text is hidden. Identity, telemetry,
status text, duration and tools are outside those drag targets. Window movement
starts after the platform drag threshold, preserving click/double-click
semantics. No setting, schema, translated string or detached Tab strip was added.

- Debug build, C++ formatting, 96-file QML format/lint and focused main.cpp
  clang-tidy pass. `title-bar-immersive-runtime`: PASS, 47.58 s, including
  blank click inertness, status-text exclusion, maximize/restore through two
  input presses and command-composer interaction.
- Windows MCP screenshots and actual drag/double-clicks were performed in
  isolated `build/test-data/session-strip-drag-review-20261001a`; exit 0.
  The window moved from (100,100) to (214,195), retaining 1120×740 geometry,
  terminal size/origin, zero grid resize requests and hidden chrome. Double-click
  maximized it, then restored it to the moved geometry. No fixture process
  remains; the existing user static-release process was not modified.
- The initial automated run aborted because the new fixture incorrectly sent
  MouseButtonDblClick directly through Qt's native input bridge (QTBUG-71263).
  That bridge requires two ordinary presses and generates the double-click
  itself. The fixture was corrected; the failed run and dump remain in the
  isolated CTest directory and are not counted as passes.

This acceptance uses local sessions and one display. It does not claim new
cross-monitor/DPI or live SSH validation.
