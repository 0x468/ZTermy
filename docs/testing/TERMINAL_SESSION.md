# Terminal session manual verification

Run the dynamic Debug build on Windows 11:

```powershell
cmake --preset msvc-dynamic-debug
cmake --build --preset msvc-dynamic-debug
.\build\msvc-dynamic-debug\ztermy.exe
```

The automated tests cover ConPTY transport, VT parsing, snapshot extraction,
queued shell input, and shutdown. The checks below cover behavior that requires
an interactive GUI, Windows input services, or visual judgment.

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
2. Move the cursor and edit the command before submitting it.

Expected:

- IME commit text reaches PowerShell exactly once.
- Wide and combining characters occupy sensible cells without corrupting
  adjacent text.
- Cursor movement agrees with the displayed cell positions.

Record the Windows display scale and IME used when reporting a failure. IME
candidate-window placement and search are not complete in the current
milestone.

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

Run:

```powershell
1..5000 | ForEach-Object { "line $_" }
```

Expected:

- The title bar and window remain responsive.
- Output continues without parser or session error status.
- Closing during output returns promptly without a runtime assertion.

The current renderer uploads a full-frame texture per delivered snapshot. This
test establishes a correctness baseline; frame-time and latency targets remain
open until the batched glyph renderer is implemented.
