# Native terminal interaction program (0.4.1-0.4.3)

## Purpose

ztermy 0.4.x turns the existing terminal foundation into an interaction-complete
daily driver for keyboard- and mouse-heavy SSH work. The program keeps the
terminal viewport as one native-rendered item, keeps protocol/state work off the
Qt Quick render thread, and favors mainstream terminal conventions with direct,
low-friction defaults for expert users.

Each milestone follows the same gate:

1. research current Windows Terminal, WezTerm, kitty, Qt, xterm, and libghostty
   behavior from primary sources;
2. audit the current ztermy call path and record any coordinate or threading
   contract that can affect correctness;
3. implement the smallest coherent vertical slice;
4. add unit, integration, translation, and runtime evidence;
5. publish an owner-facing manual acceptance list before the milestone commit.

Third-party products are behavior references only. ztermy does not copy their
source, visual assets, configuration, or branding.

## 0.4.1 - selection, clipboard, and scrollback

The first milestone fixes interaction correctness before adding more terminal
protocol surface.

### Required behavior

- Edge-drag selection scrolls vertically beyond the visible viewport with
  bounded, distance-sensitive speed.
- Selection anchors remain attached to the same terminal content while the
  viewport scrolls, output arrives, snapshots coalesce, or the window resizes.
- Cell, word, logical-line, and rectangular granularity survive the full drag
  gesture. Double-click-drag expands by word and triple-click-drag by line.
- Word boundaries are configurable and default to path-, URL-, and
  `user@host:/path`-friendly behavior.
- `Select all` covers retained scrollback, while `Select visible` remains an
  explicit viewport-only action.
- Standard-notch wheel input remains deterministic; high-resolution
  `pixelDelta` input is retained rather than discarded.
- Local scrollback actions cover line, page, top, and bottom navigation and are
  exposed through the action registry.
- Input and paste continue to return to the live prompt. New output does not
  steal a viewport that the user deliberately scrolled away from the bottom.
- Middle-click is configurable as disabled, paste, or context menu; disabled is
  the Windows-first default.
- Multiline paste remains faithful and keyboard-operable. Bracketed paste stays
  negotiated by the terminal engine rather than becoming a user-facing
  protocol switch.

### Architecture decision

Viewport row numbers are not stable selection identities. ztermy therefore uses
libghostty's selection gesture state and tracked grid references in the terminal
worker. Autoscroll is a compound worker operation: libghostty atomically scrolls
and extends the active tracked gesture, ztermy installs the returned selection,
and the session publishes one coalesced snapshot only when state changed. QML
owns only pointer presentation; `TerminalItem` converts Qt events into
platform-neutral gesture requests and bounded timer intent.

The complete contract is recorded in
`docs/adr/0103-tracked-terminal-selection-gestures.md`.

### 0.4.1 owner acceptance

Run the static Release build and compare the following behavior with Windows
Terminal using the same shell and sample text:

1. Generate at least 10,000 history lines, start a selection in the middle,
   drag above and below the viewport, reverse direction, and release. The
   original anchor must not drift, the speed should increase with overshoot,
   and holding at the oldest/newest boundary must not keep the CPU busy.
2. Double-click a path such as `user@host:/var/log/app.log`, keep dragging past
   the viewport, and verify whole-word expansion. Repeat with triple-click on a
   wrapped logical line and with Alt rectangular selection.
3. Repeat selection across Chinese wide characters, combining accents, and
   emoji. No half character may be copied. `Select all` must include retained
   history, while `Select visible` must not.
4. Test a standard mouse wheel and a precision touchpad. The former defaults to
   three rows per notch; the latter must retain fine `pixelDelta` movement.
   Customized line/page/top/bottom shortcuts must replace, rather than coexist
   with, their previous defaults.
5. Exercise right-click modes, disabled/paste/context-menu middle-click modes,
   Ctrl+Insert, Shift+Insert, copy-on-select, multiline-paste confirmation, and
   bracketed paste in a shell that enables it.
6. While an edge selection is active, open a popup, switch tabs, resize the
   window, minimize it, and disconnect the session. Autoscroll must stop and the
   next click must start a fresh gesture.

## 0.4.2 - VT keyboard, mouse, and focus protocol

The second milestone replaces the hand-written basic key encoder with the
libghostty encoder already pinned by the project.

### Required behavior

- Worker-side key encoding reads the live terminal modes immediately before the
  PTY write.
- DECCKM, application keypad, F1-F24, editing keys, Shift+Tab, modified keys,
  Alt/AltGr, NumLock, modifyOtherKeys, and negotiated kitty keyboard flags are
  covered without double-sending IME commits.
- VT mouse tracking supports the modes and encodings exposed by the pinned
  libghostty release. When an application owns the mouse, Shift always bypasses
  reporting for local text selection.
- Focus reporting is emitted only while the application requests it and is
  deduplicated across temporary QML focus changes.
- Motion events are bounded/coalesced and never block the GUI or render thread.

The UI may receive a minimal `mouseTrackingActive` snapshot flag for routing,
but encoded bytes remain a worker concern. Application cursor/keypad state must
not be mirrored into QML as a second authority.

## 0.4.3 - links and keyboard-first productivity

The third milestone builds higher-level workflows on the stable selection and
input contracts.

### Required behavior

- Explicit OSC 8 hyperlinks take priority over incrementally detected URLs,
  email addresses, paths, file-and-line references, IP addresses, and
  `host:port` values.
- Hover, modifier-click, context actions, and focus treatment are consistent in
  light and dark themes. Remote paths offer Copy, Locate in SFTP, and Insert;
  they are never misrepresented as local files.
- Quick Select labels visible viewport matches and defaults to Copy. Insert and
  Open are explicit actions.
- Keyboard Copy/Mark Mode navigates retained scrollback by cell, word, line,
  page, top, and bottom; it supports rectangular selection and switching the
  active endpoint.
- Command-oriented actions reuse `SemanticTerminalObserver` and
  `CommandBlockStore`: previous/next prompt, select/copy command, and select/copy
  output retain rich/basic/none capability and partial/truncated/interleaved
  evidence.
- OSC 52 is delivered only to the extent supported by the pinned public API:
  write-to-system-clipboard is supported; read is not advertised.
- Local drag/drop inserts shell-quoted paths. SSH drag/drop uses SFTP and a known
  remote working directory rather than inserting meaningless local paths.
- Tabs support reorder, title, duplicate, reopen-last-closed, close-other/right,
  and reconnect workflows. Reopening SSH creates a fresh connection from a
  declarative session spec; it does not claim to preserve a dead process.

## Defaults and product decisions

- `copyOnSelect`: off.
- right-click: retain the user's saved choice; new installs use the established
  ztermy context-menu default until the owner changes it.
- middle-click: disabled.
- selection bypass while a TUI owns the mouse: Shift.
- kitty keyboard protocol: application-negotiated only.
- Quick Select default action: Copy.
- OSC 52 write: enabled with bounded payload handling and no repeated modal
  confirmation.
- duplicate/reopen SSH tab: new connection using the saved profile/session spec.
- all actions are terminal-tab scoped; none enumerate or control another tab.

## Primary references

- Windows Terminal selection:
  <https://learn.microsoft.com/windows/terminal/selection>
- Windows Terminal interaction settings:
  <https://learn.microsoft.com/windows/terminal/customize-settings/interaction>
- Windows Terminal actions:
  <https://learn.microsoft.com/windows/terminal/customize-settings/actions>
- Qt wheel events: <https://doc.qt.io/qt-6/qwheelevent.html>
- xterm control sequences:
  <https://invisible-island.net/xterm/ctlseqs/ctlseqs.html>
- Ghostty VT reference: <https://ghostty.org/docs/vt/reference>
- kitty keyboard protocol:
  <https://sw.kovidgoyal.net/kitty/keyboard-protocol/>
- kitty hints: <https://sw.kovidgoyal.net/kitty/kittens/hints/>
- WezTerm copy mode: <https://wezterm.org/copymode.html>
- WezTerm quick select: <https://wezterm.org/quickselect.html>
- WezTerm hyperlink rules:
  <https://wezterm.org/config/lua/config/hyperlink_rules.html>
