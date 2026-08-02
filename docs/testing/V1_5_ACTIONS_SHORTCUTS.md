# V1.5 actions and shortcuts acceptance

Status: complete; owner accepted on 2026-08-02

## Product contract

- `Ctrl+Shift+P` opens one keyboard-first command palette for application,
  tab, and active-terminal actions.
- The C++ action registry is the single source of action IDs, translated
  metadata, availability, defaults, effective bindings, and conflict rules.
- Settings > Shortcuts searches every registered action and supports recording,
  unbinding, per-action reset, and reset all.
- Shortcut overrides persist in `settings.json`. An empty override deliberately
  leaves an action unbound so the key sequence can reach the terminal.
- A shortcut recorder rejects exact conflicts, multi-step sequences, invalid
  sequences, and unmodified printable keys. It never invokes an application
  action while recording.
- V1.5 remains window-scoped. System-wide hotkeys, multi-step chords, imported
  schemes, serial actions, SFTP actions, and arbitrary plugin actions are out of
  scope.

## Automated evidence

Environment: Windows 11, Qt 6.8.3, MSVC 2022, C++23, Ninja.

- Dynamic Debug and Static Release builds pass. Their complete CTest suites
  each report 28 of 28 tests passed, including the new action-registry,
  settings-schema, controller-dispatch, translation, and QML smoke coverage.
- The Qt translation pipeline extracts 577 finished Simplified Chinese entries
  with zero unfinished entries. The action library is an explicit translation
  source target, and the freshness, placeholder, and call-site gate passes.
- C++ clang-format, QML qmlformat, and qmllint pass without diagnostics. The
  complete clang-tidy gate analyzes all 64 production, test, and tool
  translation units with every diagnostic treated as an error and passes.
- All seven real-window gates pass serially in Dynamic Debug and Static
  Release, including work-area/maximize behavior, DWM appearance, native
  resizing, 100/125/150/200% DPI, adaptive layout, keyboard routing, and
  terminal rendering under large output.
- The layout gate switches the live QML engine and C++ action registry to
  Simplified Chinese, verifies translated shortcut-search, command-palette,
  action-label, and accessible-list text, captures both surfaces, and restores
  English. This proves the compiled catalog is applied at runtime rather than
  merely present in the executable.
- The real-window keyboard gate covers palette open, search, execution, and
  focus restoration, plus the accessible Dialog/List roles and names. It also
  records a conflicting shortcut without invoking the bound action,
  successfully records `Ctrl+Alt+P`, persists it through the controller,
  restores `Ctrl+Shift+P`, unbinds Find and proves the application no longer
  intercepts it, restores Find, and repeats the recorder route in regular and
  compact window sizes.
- Static Release generates a 17,680,097-byte self-contained portable ZIP and a
  14,766,080-byte per-user MSI. WiX ICE/contract inspection passes under a
  normal desktop token with the three previously reviewed ICE61, ICE69, and
  ICE91 warnings. The checksummed release bundle contains exactly both
  artifacts and its two manifests.

## Owner manual acceptance

Result: passed by the owner on 2026-08-02 in both the requested interaction
flow and the packaged application.

Use the latest Dynamic Debug executable. Test both English and Simplified
Chinese before approving the V1.5 commit.

### 1. Command palette

1. Start on Hosts and press `Ctrl+Shift+P`.
2. Search for `settings`, use Up/Down, and press Enter.
3. Open the palette again, clear the search, and press Escape.
4. Open a terminal, search for `history`, and run it.

Expected: the palette is themed, centered, keyboard navigable, and restores
focus when cancelled. Enter opens Settings or the History side panel without a
duplicate tab. Terminal-only actions are visibly unavailable when no terminal
exists and become available after one opens.

### 2. Record and persist a shortcut

1. Open Settings > Shortcuts and search for `command palette`.
2. Select its shortcut field, then press `Ctrl+Alt+P`.
3. Close Settings and verify `Ctrl+Alt+P` opens the palette while the old
   `Ctrl+Shift+P` no longer does.
4. Restart ztermy and repeat the check.

Expected: recording starts with a clear focus treatment, saves immediately,
and survives restart. Normal application actions cannot fire while a recorder
is active.

### 3. Conflict and terminal-input protection

1. Record `Ctrl+Shift+F` for Command palette.
2. Try to record the unmodified printable key `A`.
3. Press Escape while recording.

Expected: the first attempt names the conflicting Find action; the second is
rejected because it would steal terminal text input; Escape cancels without
changing the current shortcut.

### 4. Unbind and reset

1. Unbind Find in terminal (`Ctrl+Shift+F`).
2. Open a terminal and press `Ctrl+Shift+F`.
3. Return to Shortcuts, reset that action, then use Reset all.

Expected: an unbound action does not open ztermy Find and the sequence is left
for the active terminal. Per-action reset restores `Ctrl+Shift+F`; Reset all
restores every default and removes custom overrides.

### 5. Search, layout, localization, and accessibility

1. Search by action name, description, action ID, and a binding such as
   `Ctrl+Tab`.
2. Resize from a wide window to the minimum supported width.
3. Repeat the palette and Shortcuts checks in Simplified Chinese using
   Tab/Shift+Tab, arrows, Enter, Space, and Escape.

Expected: results update immediately; compact rows stack without clipping;
there are no untranslated V1.5 strings, focus traps, hidden primary actions, or
pointer-only controls. Focus indicators and accessible names describe the
action rather than only its icon.

### 6. Static Release and portable package

Repeat the basic palette, custom binding, restart, and reset flow from the
Static Release portable package.

Expected: no missing runtime dependency, console window, assertion, crash, or
configuration path leak. Portable settings retain the override under the
portable data root.
