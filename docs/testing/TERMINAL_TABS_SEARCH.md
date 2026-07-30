# Terminal tabs and search manual verification

Run the dynamic Debug build on Windows 11:

```powershell
cmake --preset msvc-dynamic-debug
cmake --build --preset msvc-dynamic-debug
.\build\msvc-dynamic-debug\ztermy.exe
```

Automated tests cover session ownership, tab activation and closure, scrollback
search, soft-wrapped matches, case matching, Unicode queries, wraparound, and
selection clearing. The checks below cover concurrent interactive sessions and
visual behavior.

## Concurrent terminal tabs

1. Wait for the initial PowerShell prompt.
2. Click the `+` button twice to create two more local terminals.
3. In each tab, run a distinct long-lived command or leave recognizable output.
4. Switch repeatedly between all three tabs.
5. Close the active middle tab, then close a background tab.
6. Create another tab and close the application while output is active.

Expected:

- Every tab owns an independent PowerShell session and keeps its own output.
- Background sessions continue running without replacing the active viewport.
- Switching restores the selected tab's latest snapshot and status.
- Closing a tab stops only that tab's worker and selects a surviving neighbor.
- Creating and closing tabs never freezes, crashes, or produces a CRT dialog.

## Search current screen and scrollback

1. Produce at least 100 numbered output lines, including several occurrences of
   a distinctive ASCII word and one Chinese phrase.
2. Press Ctrl+Shift+F and enter the ASCII word using different letter case.
3. Press Enter repeatedly, then Shift+Enter repeatedly.
4. Toggle `Aa` and repeat the search with exact and mismatched case.
5. Search for the Chinese phrase.
6. Search for text that crosses a visually wrapped line.
7. Press Escape.

Expected:

- The search field receives focus and the counter reports the current and total
  matches.
- Enter selects the next match; Shift+Enter selects the previous match.
- Navigation wraps at both ends without losing the match selection.
- Default matching ignores ASCII letter case; `Aa` requires exact case.
- Unicode text matches exactly and wrapped logical lines are searchable.
- Off-screen matches scroll into view.
- Escape closes the search overlay, clears the search selection, and returns
  keyboard focus to the terminal.
- Ctrl+F remains available to the terminal application and does not open the
  ztermy search overlay.

## Per-tab search state

1. Open two tabs and place different searchable output in each.
2. Search for a term in the first tab.
3. Without closing the search overlay, switch to the second tab and search for a
   different term with `Aa` toggled.
4. Switch between the tabs while the overlay remains open.

Expected:

- Query, case option, result counter, selection, and scroll position follow the
  active tab.
- Search navigation never selects or scrolls a background session.
- Closing a searched tab leaves the surviving tab's search state intact.

## Keyboard tab workflow

1. Keep the terminal viewport focused.
2. Press Ctrl+Shift+T twice.
3. Press Ctrl+Tab repeatedly, then Ctrl+Shift+Tab repeatedly.
4. Press Ctrl+Shift+W on the active tab.
5. Press Tab until focus reaches the title-bar terminal tabs, their close
   buttons, and the new-tab button. Activate each kind of control with Space
   and Enter.

Expected:

- Ctrl+Shift+T creates one independent local terminal per key press.
- Ctrl+Tab and Ctrl+Shift+Tab wrap through the tabs in opposite directions.
- Ctrl+Shift+W closes only the active tab and selects a surviving neighbor.
- Every title-bar action has a visible focus outline and an accessible name.
- Space and Enter match the corresponding mouse action.
- Activating or switching a terminal returns keyboard focus to its viewport.
- Terminal input never receives the bytes for a handled application shortcut.

## Limits and current behavior

- At most 32 terminal tabs may be open at once.
- Case-insensitive matching currently folds ASCII letters. Non-ASCII searches
  are exact, including letter case.
- Search is snapshot-based. New output is included the next time the query or
  navigation command runs.
