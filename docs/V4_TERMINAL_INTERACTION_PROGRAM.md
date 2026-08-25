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

### Architecture decision

Qt input is normalized into platform-neutral semantic events, queued without
blocking the GUI thread, and encoded by the session worker from the live
libghostty state immediately before the PTY or SSH write. Mouse motion and focus
updates coalesce in bounded queues. IME commit remains a dedicated UTF-8 path,
and the semantic shell observer never receives negotiated CSI key sequences.

The complete contract is recorded in
`docs/adr/0110-worker-side-vt-input-encoding.md`.

### 0.4.2 owner acceptance

Use the same static Release build for local PowerShell and an SSH profile. For
items involving an interactive application, compare with Windows Terminal on
the same machine and shell:

1. In PowerShell and a remote POSIX shell, verify letters on the active keyboard
   layout, Ctrl/Alt shortcuts, AltGr characters, left/right modifiers, Enter,
   Backspace, Tab, Shift+Tab, Home/End, Insert/Delete, Page Up/Down, arrows,
   F1-F24, and the numeric keypad with NumLock on and off. No key may be emitted
   twice and key-up events must not type text.
2. Enter Chinese text with the Windows IME at the prompt and in `hx` or another
   full-screen editor. Preedit must remain local, commit must occur exactly
   once, candidate UI placement must follow the cursor, and AltGr/non-IME
   layout input must remain unaffected.
3. In `hx`, Vim, tmux, or another mouse-aware TUI, click, drag, hover, and wheel.
   The application must receive pointer input. Hold Shift and drag: ztermy must
   select and copy terminal text locally without also sending mouse reports.
4. In an alternate-screen application that enables alternate scrolling but not
   mouse tracking, use the wheel. It must navigate through application key input
   rather than moving ztermy scrollback. After exit, the wheel must resume local
   scrollback behavior.
5. Use an application that enables focus reporting, switch between ztermy,
   another window, settings, and terminal tabs, then return. Each effective
   focus transition must be reported once; transient QML focus changes must not
   produce a burst of duplicate reports.
6. Resize rapidly while holding a mouse button in a mouse-aware TUI and generate
   sustained pointer motion. The UI must remain responsive, the final pointer
   position must arrive, and memory/queue growth must remain bounded.

## 0.4.3 - links and keyboard-first productivity

The third milestone builds higher-level workflows on the stable selection and
input contracts. Link recognition, selection identity, clipboard effects, and
tab recovery remain distinct layers: semantic metadata is produced off the
render thread, transient overlays are painted by the existing terminal custom
item, and application workflows are enabled through capability facts.

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
  `CommandBlockStore`: the latest command can be copied with its exact or
  approximate confidence identified, while output can be copied only when the
  retained block is complete. Prompt navigation and selecting semantic blocks
  are deferred until stream offsets have a stable mapping back to tracked grid
  identities; painted coordinates are never guessed.
- OSC 52 is delivered only to the extent supported by the pinned public API:
  write-to-system-clipboard is supported; read is not advertised.
- Local drag/drop inserts shell-quoted paths. SSH drag/drop uses SFTP and a known
  remote working directory rather than inserting meaningless local paths.
- Tabs support reorder, title, duplicate, reopen-last-closed, close-other/right,
  and reconnect workflows. Reopening SSH creates a fresh connection from a
  declarative session spec; it does not claim to preserve a dead process.

### Architecture decision

- OSC 8 URI text lives in a snapshot-local intern table. Cells do not own
  strings; compact spans refer to the table and may cross wrapped rows.
- Bounded detection on changed logical lines adds typed spans. Explicit OSC 8
  spans claim their cells and always take priority over detected links.
- Pointer routing is deterministic: Shift selection first, Ctrl+click link
  activation second, active TUI mouse reporting third, then local pointer
  behavior. A link opens only on release below the platform drag threshold.
- Quick Select considers the current viewport and self-renders prefix-free
  labels inside `TerminalItem`; it never creates one QML object per cell or
  label.
- Copy Mode keeps its anchor and endpoint as libghostty tracked references on
  the worker. Visible row indexes are never treated as stable identities.
- OSC 52 is write-only. The synchronous libghostty callback copies one bounded
  payload, sessions drain it after releasing the engine lock, and the GUI
  thread performs the clipboard mutation.
- Local file drops insert shell-quoted paths without executing. SSH file drops
  upload through the tab's SFTP capability to a known remote directory; an
  unknown destination is requested explicitly. Text drops use normal paste.
- Duplicate and reopen create fresh sessions from declarative descriptions.
  Reconnect replaces a disconnected session behind the same tab identity.
- The controller exposes typed-link, selection, session, SFTP, reconnect, modal
  mode, and semantic-command capability facts. QML menus consume those facts
  rather than inferring terminal state from presentation details.

The complete contract, including performance and lifetime constraints, is
recorded in `docs/adr/0111-semantic-terminal-workflows.md`.

### Implementation order

Each slice is researched, tested, and kept independently reviewable before the
next begins:

1. extend snapshots with wrapped-row metadata, interned OSC 8 link tables, and
   compact typed spans; add the bounded logical-line detector and precedence
   tests;
2. implement themed hover, Ctrl+click release activation, context actions, and
   `ActionContext` link/SFTP capabilities;
3. add current-viewport Quick Select with terminal-node rendering and
   deterministic copy/insert/open keyboard actions;
4. add worker-owned tracked-reference Copy Mode and bounded latest-command/
   output actions, preserving rich/basic/none and
   partial/truncated/interleaved evidence;
5. register and drain bounded OSC 52 write effects outside the engine lock, then
   marshal clipboard writes to the GUI thread;
6. route text/local-file/SSH-file drops through paste, shell quoting, or SFTP
   without implicit execution;
7. add tab reorder/title/duplicate/reopen-last-closed/close-other/right and make
   reconnect discoverable while preserving fresh-session semantics;
8. finish translations, focused runtime evidence, full Debug and static Release
   gates, package generation, and owner acceptance notes.

### 0.4.3 owner acceptance matrix

Run the static Release build. Use local PowerShell, the saved SSH password and
key profiles, and a remote mouse-aware TUI where the case asks for one.

| Area | Manual test | Expected result |
| --- | --- | --- |
| OSC 8 precedence | Print an OSC 8 link whose visible text also contains a URL, then hover and Ctrl+click it. | The explicit OSC 8 target is shown/opened once; the detected URL never replaces it. Wrapped text remains one target. |
| Detected targets | Print HTTP/HTTPS URLs, email, Windows/POSIX paths, `path:line:column`, IPv4/IPv6, `host:port`, and a Git hash in light and dark themes. | Each supported type receives one stable hover treatment and only applicable Open/Copy/Insert/Locate-in-SFTP actions. Remote paths are never opened as local files. |
| Pointer precedence | Enable mouse reporting in `hx`, Vim, or tmux. Click normally, Shift-drag, and Ctrl+click a visible link; toggle Ctrl without moving the mouse and try a small drag. | Normal input reaches the TUI, Shift always selects locally, stationary Ctrl updates the link cursor/tooltip, and a drag never accidentally opens the link. |
| Quick Select | Press Ctrl+Shift+Space with many mixed targets visible, type labels, then repeat after scrolling/resizing. Exercise plain, Shift, Ctrl, Enter, and Escape actions. | Labels are deterministic, collision-free, confined to the viewport, remain responsive, recompute after viewport changes, and perform Copy/Insert/Open/Cancel exactly once. |
| Copy Mode | Enter Ctrl+Shift+X after creating long wrapped scrollback. Navigate by cell/word/line/page/top/bottom, switch endpoints, and select rectangle/wide Unicode while output continues. | The anchor does not drift; wide/combining characters are never split; `y`/Enter copies and exits, Escape cancels, and no key leaks to the shell. |
| Command blocks | In rich shell integration and then basic/no-integration modes, open the terminal context menu after a completed command, an approximate command, and a long truncated or interleaved block. | Copy Last Command identifies approximate evidence; Copy Last Command Output is enabled only for complete retained output, and no omitted output is presented as complete. |
| OSC 52 | Run a trusted local and remote command that writes a small OSC 52 text payload, then try repeated and oversized payloads. | The latest valid payload reaches the Windows clipboard without a modal prompt; reads are not offered; oversize data is rejected/bounded and UI responsiveness/memory stay stable. |
| Local drop | Drop one and multiple files, including names with spaces and shell metacharacters, into local PowerShell. Drop plain multiline text separately. | File paths are correctly quoted and inserted but not executed; text follows normal paste and multiline confirmation/bracketed-paste behavior. |
| SSH drop | Drop a local file onto an SSH tab with a known remote CWD, then with only an SFTP/home directory, and finally with no known destination. | Upload uses the documented fallback order and exposes transfer progress/cancellation; an unknown destination is requested, no meaningless local path is typed, and nothing executes implicitly. |
| Tab workflows | Reorder tabs; set a title; duplicate local and saved SSH tabs; close other/right; close and reopen up to ten tabs. | Layout and titles persist as specified. Duplicate/reopen starts a new process/connection from the stored description and never claims to restore a dead process or old scrollback. |
| Reconnect/lifetime | Disconnect SSH, use the visible reconnect action, then rapidly close/reopen/switch tabs while links, Quick Select, Copy Mode, OSC 52, or upload work is pending. | Reconnect retains the tab identity and valid UI state. Late worker/effect results are discarded, transient modes cancel, no stale popup/action survives, and the process does not crash or hang. |
| Performance | Fill 10,000+ lines containing link-like text, resize and scroll rapidly, enter/exit Quick Select repeatedly, and monitor CPU/memory. | Painting does not run detection, no per-cell QML tree appears, interaction remains responsive, queues stay bounded, and idle CPU returns near baseline. |

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
