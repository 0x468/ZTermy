# V2.2 manual acceptance

Use the static Release build for final visual and real-host checks. Keep the
test host limited to disposable files and directories. Never paste credentials
into logs, screenshots, or issue text.

## 1. Hosts workspace

1. Open ztermy with no terminal tabs.
2. Check dark and light themes at approximately `1120 x 800` and `500 x 360`.
3. Type part of a saved profile name in the command row, clear it, then enter a
   quick target in `user@host[:port]` form.
4. Activate a saved host by clicking the card and by focusing it with Tab and
   pressing Enter.
5. Open the card's More menu with the keyboard.
6. Open New host and Edit host.

Expected:

- there is one compact command row and no dashboard hero;
- host rows stay about 64-72 logical pixels high;
- the whole host row connects, while edit/delete/credential actions remain
  secondary;
- the editor occupies a fixed right inspector and reduces the host-list width;
- group completion, generated profile-name selection, Escape, and focus restore
  remain stable.

## 2. Terminal workspace

1. Open a local terminal and a saved SSH profile.
2. Exercise SFTP, command composer, search, session log, command snippets, and
   the More menu using both pointer and keyboard.
3. Move the workbench from left to right, resize it, close it, and reopen it.
4. In composer, verify Enter sends and Shift+Enter inserts a newline.
5. In history, switch between current-profile and global scopes, refresh, and
   use Run, Insert, and Save as snippet.
6. Move the pointer over Hosts, Settings, a terminal tab, Minimize, and
   Maximize; hold it still on each target for at least two seconds.
7. With one terminal tab, verify that the add button immediately follows the
   tab. Click add, choose both menu entries in turn, then close the last
   terminal tab.
8. Open Find from the toolbar, then click the same toolbar icon again.

Expected:

- the 26-pixel terminal toolbar never covers the viewport;
- every supported action has a themed tooltip and visible focus state;
- the workbench resize boundary remains narrow and does not steal pointer input
  from its controls;
- closing search, composer, menus, and workbench returns focus to the terminal.
- hover surfaces remain visible while the pointer is stationary; Maximize still
  opens Windows 11 Snap Layouts;
- add opens the new-terminal menu instead of silently starting a default shell,
  there is no blank strip between one tab and add, and closing the last session
  returns to Hosts;
- the Find toolbar icon opens and closes the search surface symmetrically.

In Settings, Apply a valid change and then Discard another draft. Both success
messages should remain readable briefly and fade away after about four seconds;
validation or storage errors should remain visible for correction.

## 3. Integrated SFTP

Use the configured key profile for `testkey@192.168.1.25`.

1. Open SFTP and verify the first listing resolves to the remote user's home
   directory.
2. Use Home, Parent, Recent paths, editable path, Copy path, Refresh, Filter,
   Hidden, Upload, New folder, rename, download, and the `..` entry.
3. Resize the workbench below and above 430 logical pixels.
4. Navigate to a path without permission, then return to the prior directory.
5. In Settings > SFTP, toggle both defaults, Apply, restart, and open a new SSH
   session.

Expected:

- a narrow browser moves refresh/upload/new-folder into More without clipping;
- Copy path places the exact current POSIX path on the clipboard;
- a permission failure is shown above the last successful listing and does not
  replace that listing with a full-surface dead end;
- hidden-file and delete-confirmation defaults survive restart and affect newly
  opened SFTP sessions; the per-session Hidden control remains independent.

## 4. Transfers

1. Upload and download a disposable file large enough to observe progress.
2. Cancel while progress is changing rapidly.
3. Retry an interrupted task, dismiss a completed task, and clear completed
   tasks.
4. Close the owning terminal tab while SFTP or a transfer is active.

Expected:

- cancel remains effective despite progress updates;
- percent, transferred bytes, total bytes, and task state remain coherent;
- retry/dismiss/clear-completed remain clickable;
- no pause/resume controls are shown because the engine does not yet provide a
  resumable-transfer contract;
- tab close and application shutdown do not hang, assert, or corrupt the heap.

## 5. Windows and terminal regressions

Repeat the established V1 gates:

- maximize/restore, edge resize, minimum size, Snap Layout hover, and Win+Z;
- light/dark Acrylic, Transparent, Mica, and Mica Alt;
- Chinese IME preedit in the middle of ASCII text and wide-character cursor;
- rapid resize during a full-screen terminal application;
- selection across wide characters, multiline-paste dialog, search, scrollback,
  and scrollbar hit testing;
- clean exit after local terminal, SSH, SFTP, and transfer activity.

Expected: behavior remains identical to the accepted `0.2.1` gates, with no
legacy frame flash, out-of-work-area maximized edge, crash dialog, or heap
assertion.

## Automated preflight

Evidence collected for the `0.2.2` candidate on 2026-08-08:

- static Release build completed;
- 42 of 42 CTest tests passed;
- translation, QML lint, C++/QML format, and clang-tidy gates passed;
- layout, keyboard, terminal-render, native-resize, and 100%-200% DPI runtime
  smokes passed;
- the retained dark/light compact/regular Hosts and Settings captures, New Host
  inspector, history, command-snippet, composer, Find, and real-host SFTP
  captures were visually reviewed against the frozen V2.2 density and hierarchy
  contract;
- the opt-in `testkey@192.168.1.25` real-host suite passed host-key
  observation, explicit private-key authentication, PTY open/resize/close, and
  a 20-entry SFTP home-directory listing;
- the opt-in remote shell-history test passed against the same host.
- the read-only integrated GUI smoke connected to `testkey@192.168.1.25`,
  opened SFTP from the visible terminal toolbar, resolved `/home/testkey`,
  copied the path, traversed the file list by keyboard, verified both adaptive
  toolbar widths, verified that a denied `/root` listing preserved the current
  home listing and recovered through Home, closed the workbench, and cleaned up
  its terminal session.

The read-only integrated GUI gate can be repeated with the same non-secret
host variables:

```powershell
cmake --build --preset msvc-static-release --target ztermy_real_host_ui_runtime_smoke
```

It creates an isolated profile, verifies the expected host fingerprint,
connects with the configured private key, opens SFTP through the visible
terminal action, checks the home listing, keyboard focus, path copy, adaptive
toolbar, workbench close, and terminal-tab cleanup. It never uploads, downloads,
creates, renames, or deletes a remote entry.

Set `ZTERMY_TEST_SFTP_DENIED_PATH` to a known unreadable remote directory (for
the maintained test host, `/root`) to additionally verify that a permission
error preserves the last successful listing and that Home navigation recovers.

These checks prove the transport and model paths, but do not replace the
pointer/keyboard/visual checks in sections 1-5.

Before manual acceptance, run:

```powershell
cmake --build --preset msvc-static-release
ctest --test-dir build\msvc-static-release --output-on-failure
cmake --build --preset msvc-static-release --target ztermy_translation_gate all_qmllint ztermy_format_check ztermy_qml_format_check ztermy_clang_tidy_check
```

Real-host password authentication remains opt-in and must be run separately so
CTest never waits for a password prompt.
