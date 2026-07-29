# Terminal session manual verification

Run the dynamic Debug build on Windows 11:

```powershell
cmake --preset msvc-dynamic-debug
cmake --build --preset msvc-dynamic-debug
.\build\msvc-dynamic-debug\ztermy.exe
```

The automated tests cover ConPTY transport, split VT parsing, true-color cell
styles, primary/alternate-screen restoration, erase and cursor modes, resize,
wide CJK cells, combining graphemes, emoji cells, snapshot extraction, queued
shell input, IME preedit layout, and shutdown. The checks below cover behavior
that still requires an interactive GUI, Windows input services, font fallback,
or visual judgment.

## Basic session

1. Wait for the initial prompt.
2. Confirm the prompt starts in the current user's home directory.
3. Type `Write-Output ztermy-ok` and press Enter.
4. Resize the window repeatedly from narrow to wide.
5. Press Ctrl+C at an empty prompt.
6. Close the application.

Expected:

- Typed characters appear once and in order.
- `ztermy-ok` appears, followed by a new prompt.
- The terminal grid follows the viewport size without a freeze or crash.
- Ctrl+C is delivered to PowerShell.
- Closing produces no assertion, runtime dialog, or lingering ztermy process.

## Formatting and full-screen behavior

1. Run `Get-ChildItem`.
2. Run a program that uses the alternate screen, such as a locally installed
   terminal editor.
3. Exit the program and return to PowerShell.

Expected:

- ANSI foreground/background colors and cursor position are plausible.
- Alternate-screen content does not overwrite the restored PowerShell screen.
- Resize remains stable while the full-screen program is active.

## Unicode and IME

1. Enter ASCII, Chinese text through the Windows IME, a wide CJK character, a
   combining-mark sample, and emoji.
2. Commit Chinese text, then move the terminal cursor across both its leading
   and trailing cells.
3. Type `hello`, move the cursor between `e` and the first `l`, and compose
   `nihao` without committing it immediately.
4. Move the IME composition caret across both Latin and Chinese preedit text.
5. Commit the composition, then edit and submit the command.

Expected:

- IME commit text reaches PowerShell exactly once.
- Wide and combining characters occupy sensible cells without corrupting
  adjacent text.
- The solid cursor covers a complete CJK glyph rather than half of it.
- While composing in the middle of `hello`, the `llo` suffix moves right as
  the preedit grows instead of being overwritten.
- A composition caret over Chinese text is two cells wide; a caret over Latin
  text is one cell wide.
- Cursor movement agrees with the displayed cell positions before and after
  committing the composition.

During composition, the preedit text should be visible at the terminal cursor
and the IME candidate window should follow the composition caret. Record the
Windows display scale and IME used when reporting a failure. Terminal search
is covered separately by
[TERMINAL_TABS_SEARCH.md](TERMINAL_TABS_SEARCH.md).

## Scrollback

1. Run `1..100 | ForEach-Object { "history line $_" }`.
2. Scroll upward with the mouse wheel until older lines are visible.
3. Leave the viewport in history and run output from another process if one is
   already active.
4. Scroll down to the prompt, then scroll up again and press a normal character.

Expected:

- Wheel-up reveals older output and wheel-down returns toward the prompt.
- The cursor is hidden while viewing history.
- New output does not forcibly move a history viewport to the bottom.
- Typing returns to the active prompt and clears any selection.

## Selection and clipboard

1. Drag from left to right across part of one output line.
2. Press Ctrl+Shift+C and paste into Notepad.
3. Drag upward or backward across multiple terminal lines and copy again.
4. Hold Alt while dragging a rectangular region and copy it.
5. Single-click the terminal without dragging.
6. Put `Write-Output pasted-ok` on the clipboard and press Ctrl+Shift+V.
7. Copy two lines of text, paste them at a PowerShell prompt, and inspect the
   command line before executing it.

Expected:

- Selected cells use the blue selection color and reversed drag directions work.
- Copied text contains only the selected cells; soft-wrapped lines are unwrapped.
- Alt+drag copies a rectangular selection.
- A click without dragging clears the previous selection.
- Ctrl+Shift+V inserts clipboard text exactly once.
- Multiline paste is encoded according to the terminal's bracketed-paste mode;
  ztermy never logs clipboard contents.

## Sustained output

Run the opt-in ConPTY and Qt event-loop gate first:

```powershell
$env:ZTERMY_RUN_LOCAL_OUTPUT_GATE = "1"
ctest --test-dir build/msvc-dynamic-debug `
  -R "^local-terminal-session$" --output-on-failure
Remove-Item Env:ZTERMY_RUN_LOCAL_OUTPUT_GATE
```

The gate starts a real PowerShell session, produces 20,000 lines, waits for a
unique completion marker in the terminal snapshot, and runs a 10 ms Qt
heartbeat concurrently. It requires at least five heartbeat ticks and five
progressive snapshots, then requires session shutdown within two seconds. This
proves the ConPTY, parser, snapshot-coalescing, and GUI-thread event-loop path;
it does not replace visual GPU-rendering checks.

For the interactive renderer check, run:

```powershell
1..5000 | ForEach-Object { "line $_" }
```

Expected:

- The title bar and window remain responsive.
- Output continues without parser or session error status.
- Closing during output returns promptly without a runtime assertion.
- On close, the Debug log contains `Terminal session metrics` with snapshot
  production, delivery, coalescing, damage, and snapshot-build timing.
- After at least 120 repainted frames, the Debug log contains `renderer timing`
  with CPU paint and texture-creation P95 values. If fewer frames were
  repainted, run the sustained-output command again before closing.

The current renderer uploads a full-frame texture per delivered snapshot. This
test and the accompanying metrics establish the baseline for damage-aware and
batched renderer work.

## Input queue latency

Run the opt-in local runtime gate from an x64 Visual Studio developer shell:

```powershell
$env:ZTERMY_RUN_LOCAL_LATENCY_GATE = "1"
ctest --test-dir build/msvc-dynamic-debug `
  -R "^local-terminal-session$" --output-on-failure
Remove-Item Env:ZTERMY_RUN_LOCAL_LATENCY_GATE
```

The gate starts a real PowerShell ConPTY session and enqueues 120 single-byte
input events at 5 ms intervals. It waits until the worker has dequeued every
sample, then requires `inputQueueP95Us` to be no greater than `16000`. The
synthetic input is not printed by the test or included in session metrics.

For an interactive cross-check:

1. Start a fresh local terminal tab in the dynamic Debug build.
2. Type and edit enough commands to enqueue at least 100 individual key-input
   events. Include cursor movement and text inserted in the middle of a line.
3. Close that terminal tab normally.
4. Open the newest Debug log and find its final `Terminal session metrics`
   entry.

Expected:

- `inputQueueSamples` is at least 100.
- `inputQueueP95Us` is no greater than `16000`.
- P50, P95, P99, and maximum values are numeric and non-negative.
- The log contains no typed command text, terminal input, clipboard content,
  password, passphrase, or private-key content.

This metric covers only the time from the GUI session enqueue to the local
worker dequeue. It does not include PowerShell processing or terminal
rendering.

## Crash diagnostics

Normal application logs are written under:

```text
%LOCALAPPDATA%\ztermy\ztermy\logs
```

Windows unhandled exceptions and MSVC Debug CRT assertions additionally create
a timestamped `.dmp` file under:

```text
%LOCALAPPDATA%\ztermy\ztermy\crashes
```

When reporting an intermittent crash, preserve the newest log and dump and note
the last visible action. Dumps can contain fragments of process memory,
including terminal content, so keep them local and do not publish them without
reviewing the disclosure risk.
