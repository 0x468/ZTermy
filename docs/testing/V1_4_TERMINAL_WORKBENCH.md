# V1.4 terminal workbench acceptance

Status: complete; owner accepted on 2026-08-02

## Automated evidence

Environment: Windows 11, Qt 6.8.3, MSVC 2022, C++23, Ninja.

- Dynamic Debug and Static Release builds pass. Their complete CTest suites
  each report 27 of 27 tests passed.
- Focused history/workbench regression tests pass for application settings,
  quick-command storage and parsing, SSH terminal sessions, and application
  controller behavior.
- Translation generation reports 513 finished and zero unfinished Simplified
  Chinese entries. The translation gate verifies freshness and placeholders.
- C++ clang-format, QML qmlformat, and qmllint gates pass without diagnostics.
- The complete clang-tidy gate analyzes all 62 production, test, and tool
  translation units with every diagnostic treated as an error and passes.
- The opt-in real-host test
  `readsRemoteShellHistoryOnRealHost` passes against the owner's configured
  key-authentication fixture. It opens a bounded auxiliary channel, receives a
  supported Bash/Zsh/Fish result, and does not send input to the interactive
  PTY.
- All seven real-window gates pass serially: work-area/maximize behavior,
  DWM appearance, native resizing, 100/125/150/200% DPI, adaptive layout,
  keyboard routing, and terminal rendering under large output.
- The keyboard route explicitly focuses the terminal toolbar and workbench
  controls, opens History and Scripts with Enter, moves the panel to the
  right, closes it, verifies focus returns to the terminal, and toggles the
  Composer. It also verifies that the Composer receives focus when opened and
  returns it on Escape. It also verifies that moving the panel right leaves a
  seven-pixel resize handle rather than an input-blocking overlay, Composer
  Shift+Enter inserts a line, and Enter sends and clears the editor. The gate
  passes in Dynamic Debug and Static Release.
- Every V1.4 icon action in the workbench and Composer uses the same strong
  focus and Enter/Space activation contract; row actions no longer depend on a
  pointer-only path.
- Tooltip and terminal overflow-menu surfaces use shared ztermy controls with
  dynamic light/dark colors, rounded floating backgrounds, themed hover/focus
  states, and bounded wrapping for long script descriptions. Both Dynamic
  Debug and Static Release compile these QML components successfully, and
  qmllint reports no diagnostics.
- The Static Release portable executable passes the QML/native-window smoke
  test from its packaged directory. The self-contained ZIP is 17,596,472
  bytes and includes `portable.flag`.
- The MSI is generated successfully and its WiX ICE/contract inspection passes
  outside the workspace sandbox. The inspection confirms per-user LocalAppData
  installation, one executable, product icon, Start-menu shortcut,
  same-version upgrade, and uninstall folder removal.

## Owner manual acceptance

Result: passed on 2026-08-02, including the final dark/light tooltip and
terminal overflow-menu visual review.

Use the latest Dynamic Debug build unless a step explicitly names Static
Release. Do not use a production server or save a real secret as a quick
command.

### 1. Toolbar and split layout

1. Open a local terminal and an SSH terminal.
2. Confirm the per-terminal toolbar shows identity/address where applicable,
   History, Scripts, Composer, Find, and More.
3. Open History. Resize its boundary, move it to the right, switch tabs, and
   return.
4. Close it with its own close button and reopen it from the toolbar.

Expected: the panel starts on the left, reflows rather than covers the terminal,
stays within a usable width, and preserves page/side/width for that live tab.
The other tab keeps independent state. Its close button, Escape, and toolbar
toggle all work.

### 2. History providers

1. In a local PowerShell tab, open History and select Refresh.
2. Connect to `testkey@192.168.1.25` with the configured `id_ed25519`, open
   History, and refresh it repeatedly while typing normally in the terminal.
3. Search rapidly for a known harmless command, clear the query, and switch
   between the current profile and Global scopes.

Expected: loading and typing in the search box never freeze input or rendering;
PowerShell and the remote Bash/Zsh/Fish history appear or show an honest
empty/unsupported error; stale refresh results never replace the newest
request. Each scope shows a correct count. Global labels entries by open tab
and is not retained after those tabs close.

### 3. History actions and code snippets

1. Choose a harmless history entry and select Insert.
2. Choose Run on another harmless entry.
3. Save an entry as a code snippet, edit its name/description/shell scope, and
   save it.

Expected: Insert places text at the active terminal without Enter. Run sends
immediately with one Enter and no confirmation dialog. Saving does not run the
command.

### 4. Scripts/code-snippet lifecycle and keyboard

1. Create at least three harmless code snippets in Scripts.
2. Search, edit, reorder with the buttons and Alt+Up/Alt+Down, then switch tabs.
3. Press Enter on a selected row, use Ctrl+Enter for Insert, and Delete for
   deletion.
4. Reject the delete confirmation once, then accept it. Restart ztermy.

Expected: order and CRUD changes persist globally; keyboard and pointer paths
match; deletion never occurs before confirmation; Enter runs immediately and
Ctrl+Enter inserts without executing. Full recording/running automation is not
presented as complete in V1.4.

### 5. Composer and Find coexistence

1. Open the workbench, Composer, and Find at the same time.
2. Resize the workbench and Composer. Click a snippet chip, hover it, and use
   Shift+click on another chip.
3. Type a one-line command and press Enter; then type two lines using
   Shift+Enter before sending.

Expected: all three regions remain usable and the terminal receives correct
row/column updates. Composer text remains attached to that live tab while
switching. Hover exposes the full snippet and activation hint; click inserts,
Shift+click sends, Enter sends, and Shift+Enter adds a line without sending.

### 6. Right-side interaction and resizing

1. Move History to the right and move the pointer across its search field,
   scope buttons, history rows, and outer splitter.
2. Search, switch scope, activate row actions, and drag only the splitter.

Expected: the horizontal resize cursor appears only over the narrow inner-edge
splitter. Every control on the rest of the right panel remains clickable and
focusable; dragging the splitter resizes without overlaying the panel.

### 7. Window, localization, and accessibility

1. Switch between English and Simplified Chinese.
2. Exercise Tab/Shift+Tab, arrow keys, Enter, Escape, focus indicators, window
   resizing, maximized mode, and the available 100--200% DPI configuration.

Expected: no untranslated V1.4 UI, clipped primary actions, focus traps,
terminal overlap, crash, or inaccessible resize boundary. Search, lists,
dialogs, and destructive actions expose meaningful accessible names.

### 8. Static Release and portable package

Run the same basic toolbar/history/Scripts/Composer flow from the Static
Release portable package.

Expected: no missing runtime dependency, no console window, preferences and
code snippets resolve under the portable data root, and closing produces no
assertion or crash dialog.
