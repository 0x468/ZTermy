# ztermy V1.1 acceptance

Candidate source commit: `c9adc55`

Status: accepted by the owner on 2026-08-01

## Implemented scope

- compact work-tab chrome with lifecycle motion, active-session state, attached
  new-tab action, and active-tab overflow containment;
- browse-and-connect Hosts dashboard with search, Quick Connect, successful
  connection recency, responsive cards, groups, and secondary management
  actions;
- compact terminal identity/action row without viewport animation;
- singleton, on-demand Settings work tab with category rail and detail page;
- global ztermy, Follow Windows, and Custom accent sources;
- shared interaction and motion tokens that follow the Windows client-area
  animation preference;
- no serial runtime, dependency, navigation, field, or placeholder control.

## Automated evidence

The dynamic Debug and static Release builds completed with MSVC, CMake, and
Ninja.

- Debug CTest: 21 of 21 passed.
- Static Release CTest: 21 of 21 passed.
- clang-tidy: 46 of 46 translation units passed with warnings as errors.
- clang-format passed.
- All 21 QML files passed qmlformat and qmllint.
- Real-window work-area, appearance, resize, UI layout, keyboard, terminal
  render/responsiveness, and DPI gates passed.
- The DPI gate passed at 100%, 125%, 150%, and 200%.
- Dark and Light screenshots were retained at regular and compact sizes under
  `build/msvc-static-release/test-data/ui-layout-smoke`.
- The portable archive and per-user MSI were generated.

WiX ICE validation could not run because the local Windows Installer service
was unavailable and returned WiX error 217. The MSI itself was generated.
The V1 owner acceptance already covered install, launch, uninstall, and a
Windows Sandbox installation; repeat that lifecycle only if the installer
payload is changed after this candidate.

## Owner acceptance evidence

On 2026-08-01, the owner completed the runtime checklist below and confirmed
that every item behaved as expected. This covers work-tab navigation and
overflow, Hosts and Quick Connect flows, recent connections, all appearance
and accent sources, keyboard and reduced-motion behavior, terminal and native
window regression checks, and the portable candidate.

## Owner runtime acceptance

Run `build/msvc-static-release/ztermy.exe`, then complete the following checks.

### 1. Work tabs and navigation

1. Confirm Settings is absent on startup.
2. Open Settings from the title-bar settings action, invoke the action again,
   then close the Settings tab.
3. Open at least eight local terminal tabs and activate tabs at both ends.
4. Close tabs until none remain.

Expected:

- one Settings tab is created and reselected; closing it restores the previous
  Hosts or terminal workspace;
- the active terminal tab scrolls into view and the new-tab action remains
  attached to the visible work-tab strip;
- the final-tab recovery surface appears without leaving a phantom tab gap.

### 2. Hosts and connection flow

1. Resize between a narrow list and a regular card grid.
2. Use Quick Connect once with an intentionally failed authentication and once
   successfully, without saving a profile.
3. Connect successfully to a saved password profile and a saved key profile.
4. Exercise Edit, Copy, and Delete with keyboard navigation.

Expected:

- search and connection actions remain visually primary at both widths;
- failed and unsaved direct endpoints do not enter Recent;
- a successfully connected saved profile moves to the front of Recent;
- secrets never appear in Recent, cards, status text, or logs;
- no serial action or serial configuration is present.

### 3. Appearance and accent

1. Compare Dark, Light, and System with Acrylic, Transparent, Mica, and
   Mica Alt.
2. Compare ztermy, Follow Windows, and Custom accents.
3. With Follow Windows active, change the Windows accent under
   **Settings > Personalization > Colors** without restarting ztermy.
4. Try `#FFD400` and `#173B8F` as custom accents, then enter an invalid color.

Expected:

- the application preview updates before Apply, Discard restores persisted
  appearance, and Apply survives restart;
- Follow Windows updates live;
- bright and dark custom accents retain readable primary-button text, focus,
  hover, pressed, and selected states;
- invalid custom input disables Apply;
- success and destructive states remain semantically green/red instead of
  adopting the custom accent.

### 4. Motion, keyboard, and dialogs

1. Navigate Hosts, Settings, the host editor, Quick Connect, host-key trust,
   and multiline-paste confirmation using only the keyboard.
2. Turn Windows **Animation effects** off and repeat tab, page, expandable
   panel, and dialog transitions; turn it on again.

Expected:

- focus is visible and restored to the invoking control after cancellation;
- Enter, Space, Escape, arrows, and Tab perform the expected actions;
- motion is short and purposeful when enabled and effectively immediate when
  disabled, with no layout jump under a stationary pointer.

### 5. Terminal and native window regression

1. Enter CJK text with IME in the middle of an existing command.
2. Run a full-screen terminal program, resize rapidly, exit it, and generate
   sustained output while scrolling.
3. Hover the maximize button for Snap Layouts; maximize, restore, snap, and
   resize from every edge.

Expected:

- IME composition, wide cursor cells, selection, and committed text remain
  correct;
- the terminal stays responsive, scrollback and scrollbar remain coherent,
  and the cursor returns to the correct line after the full-screen program;
- Snap Layouts, native resize cursors, work-area maximization, and caption
  controls remain correct with no legacy frame flash.

### 6. Portable candidate

Extract and run
`build/msvc-static-release/package/portable/ztermy-0.1.0-windows-x64-portable.zip`.

Expected:

- it starts without a missing-runtime dialog;
- `portable.flag` keeps application data beside the executable;
- Hosts, Settings, one local terminal, and one SSH connection behave like the
  build-tree executable.
