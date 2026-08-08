# V2.3 manual acceptance

Use the static Release build for final checks. Test remote mutations only in a
disposable directory. Never include passwords, private-key material, terminal
input, or secret-bearing command lines in screenshots or diagnostic exports.

## 1. SFTP bookmarks and new file

1. Connect to a saved SSH profile and open SFTP.
2. Bookmark the home directory, navigate elsewhere, bookmark a second path,
   then use the bookmark menu to return to each path.
3. Remove one bookmark, close the terminal and ztermy, restart, reconnect, and
   confirm the remaining bookmark is retained for that profile only.
4. At both a wide and narrow workbench width, create `ztermy-v23-empty.txt`.
5. Refresh and confirm the file is present with size 0, then delete it.
6. Complete steps 2-5 using keyboard only.

Expected: bookmarks are newest-first, never duplicated, persist after restart,
and do not leak between profiles. New file is available directly in the wide
toolbar and through More when compact. The dialog has a visible focus state,
Enter creates, Escape cancels, and errors preserve the last useful listing.

## 2. Contrast Themes and Narrator

1. Enable each Windows Contrast Theme in Settings > Accessibility > Contrast
   themes and inspect Hosts, host editor, terminal toolbar/workbench, SFTP,
   transfers, Settings, menus, tooltips, dialogs, selection, and focus.
2. With Narrator enabled, traverse the same surfaces by Tab, Shift+Tab, arrow
   keys, Enter, Space, and Escape.
3. Check the SFTP bookmark, New file, file list, transfer cancellation, and
   window caption buttons specifically.

Expected: text, icons, borders, selection, disabled state, and keyboard focus
remain distinguishable without relying on color alone. Narrator announces a
concise role/name/state for actionable controls in task order. No decorative
icon becomes a duplicate focus stop. Terminal input remains usable and secret
fields never expose their value.

Record the Windows build, chosen Contrast Theme, and any Narrator deviation.
This section cannot be replaced by a unit-test result.

## 3. Physical DPI and multiple monitors

1. Exercise 100%, 125%, 150%, and 200% scaling where hardware permits.
2. On mixed-DPI monitors, move the restored window slowly and rapidly across
   the boundary, then maximize/restore it on each monitor.
3. Repeat with SFTP open, a long terminal buffer, Settings, and a popup menu.
4. Check resize cursors, scrollbar hit targets, Snap Layout hover, Win+Z, and
   the maximized work area.

Expected: the window and popups adopt the destination monitor scale without a
legacy frame, oversized edge, clipped toolbar, lost pointer target, corrupted
terminal cells, or crash. Snap Layouts remain anchored to the visible maximize
button.

## 4. Keyboard, language, and visual QA

1. Run the Hosts, terminal, SFTP, transfer, and Settings flows once in English
   and once in Simplified Chinese.
2. Inspect approximately `1120 x 800` and `500 x 360` in light and dark themes.
3. Verify every visible label, menu, tooltip, error, empty state, and dialog;
   use keyboard-only traversal and close/reopen each transient surface.

Expected: no untranslated source text appears in Chinese, no translation is
clipped, focus returns to the originating task, and hover/focus geometry stays
stable. Reduced-motion mode removes decorative transitions without hiding
state changes.

## 5. Long-running lifecycle

1. Keep one local terminal and one SSH terminal active for at least 30 minutes.
2. Produce sustained output, search history, resize repeatedly, browse SFTP,
   and start/cancel disposable uploads and downloads.
3. Close the SFTP-owning tab during activity, reopen it, then exit ztermy while
   a transfer or SFTP request is active.

Expected: cancellation remains actionable while progress changes; no UI
heartbeat degradation accumulates; tab close and app shutdown complete without
a hang, crash dialog, heap assertion, orphaned shell, or corrupted recovery
state.

## 6. Migration and packaging

1. Back up and open copies of installed and portable data from `0.2.0`, `0.2.1`,
   and `0.2.2` with the `0.2.3` build.
2. Verify profiles, credentials, settings, shortcuts, snippets, collapsed host
   sections, workspace placement, and recent SFTP paths.
3. Add a bookmark in `0.2.3`, restart, and confirm it persists.
4. Install, launch, upgrade, and uninstall the MSI; launch the portable ZIP and
   verify `portable.flag` keeps data in the portable directory.

Expected: supported data migrates without loss, older workspace documents gain
an empty bookmark list, and malformed data fails safely. Downgrading modified
v3 workspace data to an older ztermy build is not supported. Package metadata,
About, executable resources, and artifact names all report `0.2.3`.

## Automated preflight

Evidence collected for the `0.2.3` candidate on 2026-08-08:

- dynamic Debug and static Release builds completed;
- 42 of 42 CTest tests passed in both build modes;
- 749 of 749 Simplified Chinese translations are finished and the translation,
  QML lint, C++/QML format, interface-icon, branding, and clang-tidy gates pass;
- the bounded 8 MiB history/1 MiB tail and 10,000-entry SFTP model performance
  tests pass their Debug regression ceilings;
- maximize/work-area, DWM appearance, native resize, 100%-200% DPI, regular and
  compact layout, keyboard-route, and sustained terminal-render smokes pass;
- the opt-in key-authenticated real-host GUI smoke connected to
  `testkey@192.168.1.25`, loaded `/home/testkey`, exercised a local per-profile
  bookmark, copy path, file-list keyboard navigation, New file keyboard focus,
  both sides of the 520-pixel adaptive toolbar, permission-error preservation
  for `/root`, and clean session shutdown;
- the resulting wide, narrow, and permission-error SFTP captures were visually
  reviewed and retain the approved compact hierarchy;
- portable ZIP and MSI artifacts were generated and assembled with SHA-256
  manifests under
  `build/msvc-static-release/package/release/ztermy-0.2.3-windows-x64`.

WiX ICE validation remains blocked on this workstation because the Windows
Installer service is unavailable to the WiX external validator. CPack generated
the MSI successfully; this environment exception is not recorded as an ICE
pass. Installation/upgrade/uninstall and the physical checks in sections 2, 3,
and 5 remain manual acceptance work.

Run before this manual matrix:

```powershell
cmake --build --preset msvc-static-release
ctest --test-dir build\msvc-static-release --output-on-failure
cmake --build --preset msvc-static-release --target ztermy_translation_gate all_qmllint ztermy_format_check ztermy_qml_format_check ztermy_clang_tidy_check
cmake --build --preset msvc-static-release --target ztermy_window_runtime_smoke ztermy_window_appearance_runtime_smoke ztermy_window_resize_runtime_smoke ztermy_window_dpi_runtime_smoke ztermy_ui_layout_runtime_smoke ztermy_ui_keyboard_runtime_smoke ztermy_terminal_render_runtime_smoke
```

The real-host GUI smoke remains opt-in and read-only except for its isolated
local bookmark-state exercise:

```powershell
cmake --build --preset msvc-static-release --target ztermy_real_host_ui_runtime_smoke
```

The New file mutation in section 1 is intentionally manual and disposable.
