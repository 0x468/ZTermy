# ADR 0110: Encode VT input from live terminal state on session workers

- Status: Accepted
- Date: 2026-08-26
- Milestone: 0.4.2

## Context

Qt presents keyboard and pointer input as GUI events, while the byte sequence a
terminal application expects depends on live VT state. Cursor keys change under
DECCKM, the keypad changes under application-keypad mode, modified keys can be
negotiated through modifyOtherKeys or the kitty keyboard protocol, and mouse
and focus reporting are application-controlled. Encoding those bytes in QML or
on the GUI thread would duplicate terminal state and can race output that
changes a mode immediately before the next input event.

Windows keyboard events also distinguish the physical key position from the
text produced by the active layout. Qt's logical key alone is not sufficient
for W3C/kitty key identity, especially for non-US layouts, AltGr, keypad keys,
and left/right modifiers. IME commit text is a separate input path and must not
be encoded a second time as a physical key.

The design was checked against the pinned libghostty C API, the kitty keyboard
protocol, xterm mouse/focus control sequences, Qt input-event semantics, and
the behavior documented by Windows Terminal, WezTerm, and kitty.

## Decision

1. `WindowsTerminalInput` converts a `QKeyEvent` into a platform-neutral
   `TerminalKeyEvent`. Physical identity comes from the Windows scan code;
   Qt logical-key fallback is limited to synthetic events that have no native
   scan code. Text, consumed modifiers, modifier side, lock state, keypad
   identity, press/repeat/release, and composition state remain explicit.
2. `TerminalItem` emits semantic key, mouse, and focus events. It never emits
   VT escape sequences. IME commit and paste continue through their dedicated
   UTF-8 byte paths.
3. Local and SSH session queues are bounded. Consecutive mouse-motion and focus
   events coalesce before they reach the worker. UI and render threads never
   wait for encoding or PTY/network writes.
4. The session worker calls the long-lived libghostty key or mouse encoder
   immediately before writing. Encoder options are refreshed from the same
   terminal engine that consumes output, so DECCKM, keypad, mouse format,
   modifyOtherKeys, and kitty flags have one authority and correct ordering.
5. Focus reports are emitted only when mode 1004 is active and duplicate focus
   transitions are suppressed. When the mode becomes active, the item reports
   its current focus once so an already-focused terminal is not missed.
6. Mouse tracking owns pointer input only when requested by the terminal.
   Holding Shift always bypasses remote reporting and restores local selection.
   Wheel input reports mouse buttons in mouse mode, sends cursor keys for
   alternate-scroll mode, and otherwise scrolls local history.
7. The semantic command observer sees user intent, not the encoded protocol
   bytes. Printable text and a small set of editing/control meanings are
   observed before encoding; kitty CSI sequences are never mistaken for typed
   shell text.

## Consequences

- Mode transitions and their following input are serialized on one worker.
- Keyboard-layout text and physical key identity can coexist without a US
  layout assumption.
- QML receives only the minimal routing flags needed for mouse ownership,
  alternate scrolling, and focus reporting; it does not mirror encoder modes.
- Very high-rate pointer motion is bounded by latest-value coalescing.
- The Windows adapter is isolated behind a platform namespace. A future
  platform port supplies a different event normalizer without changing the
  terminal engine or session contracts.

## Verification

- Engine tests switch live DECCKM, mouse, focus, and alternate-scroll modes and
  assert the bytes emitted by libghostty.
- UI tests cover Windows physical scan-code mapping, keypad Enter, tracked
  mouse routing, Shift selection bypass, and remote wheel routing.
- Local-session integration sends a command as semantic key events through the
  worker and verifies its result in a real ConPTY PowerShell session.
- Local and SSH session suites cover worker lifetime and queue behavior.

## References

- Qt key events: <https://doc.qt.io/qt-6/qkeyevent.html>
- Qt input methods: <https://doc.qt.io/qt-6/qinputmethodevent.html>
- xterm control sequences: <https://invisible-island.net/xterm/ctlseqs/ctlseqs.html>
- kitty keyboard protocol: <https://sw.kovidgoyal.net/kitty/keyboard-protocol/>
- Ghostty VT reference: <https://ghostty.org/docs/vt/reference>
- Windows Terminal input actions:
  <https://learn.microsoft.com/windows/terminal/customize-settings/actions>

