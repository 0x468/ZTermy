# Deferred manual acceptance ledger

## Status and rules

V2 `0.2.0` was accepted on 2026-08-03 as a personal/private daily-use baseline.
This file is retained as the authoritative regression matrix for runtime and
environment evidence that automation cannot honestly replace. `PENDING` means
only that the exact listed matrix has not been recorded against the immutable
V2 artifacts; it must not be rewritten as `PASS` without evidence.

Real-host credentials and GUI control still require the owner's current
authorization. Secrets, terminal input, and private-key contents must never be
recorded in this ledger.

## V1.6 SFTP browser

Status: `PENDING`

Prerequisites: a saved SSH profile with working password or `id_ed25519`
authentication and an unlocked credential vault.

1. Connect the SSH terminal, open the terminal side panel, and select the Files
   icon.
   Expected: the terminal remains responsive; the panel shows connecting/loading
   states and then the remote directory. Opening Files on a local terminal is
   unavailable and does not create a session.
2. Navigate by double-clicking folders, pressing Enter, editing the path field,
   using Parent, and using Refresh.
   Expected: paths are normalized, stale results never replace the latest path,
   `/` has no active parent action, and repeated navigation does not freeze input.
3. Filter names and toggle Hidden.
   Expected: filtering is case-insensitive, directories remain before files, and
   toggling Hidden preserves a valid current path.
4. Create a folder, rename it with F2 and the row action, then delete it with
   Delete and the row action.
   Expected: invalid/empty names cannot be submitted, success refreshes the
   listing, failure is visible without closing the terminal, and deletion always
   requires confirmation.
5. Move the side panel between left and right and resize it.
   Expected: controls remain interactive; only the narrow divider displays the
   horizontal-resize cursor; focus and hover styling match both themes.
6. Disconnect/reconnect and close the originating terminal tab while Files is
   active.
   Expected: the SFTP session stops promptly, no crash or heap assertion occurs,
   and no orphan network work updates another tab.

## V1.6 transfers

Status: `PENDING`

1. Upload and download an empty file, small Unicode-named file, and a file large
   enough for visible progress.
   Expected: byte totals/progress are monotonic, contents and names match, and the
   terminal remains responsive.
2. Start more transfers than the configured concurrency limit.
   Expected: excess work stays queued, starts FIFO as slots free, and the global
   transfer surface remains available after closing the Files panel.
3. Cancel queued, uploading, and downloading tasks.
   Expected: queued tasks never start; a cancelled download preserves an existing
   destination; a cancelled upload removes its `.ztermy-part-*` temporary file.
4. Exercise Skip, Replace, Rename, and Cancel for local and remote conflicts.
   Expected: no conflict is resolved implicitly; Replace publishes atomically;
   Rename uses the chosen destination; Skip and Cancel preserve existing data.
5. Lock the portable vault before a queued transfer starts, then unlock and retry.
   Expected: the task enters Needs attention without leaking a secret and resumes
   only after an explicit retry.
6. Reject and then accept an unknown/changed host key from a transfer worker.
   Expected: host-key details identify the correct host/task; rejection fails only
   that task; acceptance follows the same known-host policy as terminal SSH.

## Cross-cutting V1.6 checks

Status: `PENDING`

- Keyboard-only: Tab/Shift+Tab, arrows, Enter, Escape, F2, and Delete reach and
  operate every visible SFTP control with a visible focus indicator.
- Screen reader: toolbar actions, path/filter fields, file/folder rows, progress,
  errors, conflict choices, and destructive dialogs expose meaningful names.
- Themes: light/dark/system themes maintain readable text, borders, hover, focus,
  selection, tooltips, menus, and dialogs.
- DPI: 100%, 125%, 150%, 175%, and 200% show no clipped controls or hit-target
  mismatch; test another monitor if available.
- Windowing: maximize/restore, Snap Layout hover, resizing, and side-panel movement
  introduce no legacy frame flash or off-screen edge loss.
- Shutdown: close ztermy during listing and during each transfer phase.
  Expected: prompt clean exit with no CRT, heap, assertion, or `pwsh.exe` dialog.

## Packaging acceptance retained for V2

Status: `PENDING`

- Launch the static portable ZIP in Windows Sandbox; verify `portable.flag`, local
  data placement, vault behavior, no missing runtime, and clean folder deletion.
- Install, launch, upgrade, and uninstall the MSI in Windows Sandbox; the known
  first-install configuration delay may be recorded, but hangs or partial removal
  must be distinguished from expected user-data retention.
- Verify file identity/version/icon/start-menu entry and release checksums.

## V1.7 session productivity

Status: `PENDING`

1. Open a saved SSH profile, browse at least three remote directories, close the
   terminal, restart ztermy, and explicitly reconnect to the same profile.
   Expected: ztermy starts on Hosts without reconnecting; the SFTP recent-path
   menu is newest-first and opens valid paths; the last path, panel side/width,
   selected workbench page, and composer height are reused only after reconnect.
2. Drag one or more files from Windows Explorer over the SFTP listing.
   Expected: a themed drop target appears; dropping queues each regular file into
   the current remote directory; the terminal remains responsive; unsupported or
   empty drag data does nothing. Directory drag is not promised in V1.7.
3. Complete, fail, and cancel transfers while the transfer center is both open
   and closed.
   Expected: one non-blocking themed toast appears per terminal transition, uses
   readable light/dark colors, can be dismissed from keyboard or pointer, and
   auto-dismisses. It never steals terminal focus or repeats for an unchanged
   state.
4. Open the command palette and run File transfers, then assign and use a custom
   shortcut for that action.
   Expected: the global transfer center opens without requiring a terminal; the
   shortcut persists and conflict validation behaves like other actions.
5. Keep a local terminal and an SSH terminal open for several minutes.
   Expected: the compact connected-duration label advances once per second when
   enough width is available, does not cause terminal repaint jank, and remains
   hidden at narrow widths without clipping other toolbar actions.
6. Inspect the selected data directory after using these features.
   Expected: `workspace_state.json` contains only profile IDs, normalized remote
   paths, page/side tokens, and bounded sizes. It contains no password,
   passphrase, terminal output/input, command history, or private-key contents.

Cross-cutting expected behavior: recent-path menus, drag/drop feedback, transfer
toasts, duration metadata, and the transfer action remain keyboard accessible,
localized in English and Simplified Chinese, readable in both themes, and usable
at 100–200% DPI.

## V1.8 session logging and script-library interchange

Status: `PENDING`

1. Open a local terminal, choose Start session log, select a writable `.log`
   path, run commands that produce ASCII, CJK, color, and full-screen output,
   then stop logging and close the tab.
   Expected: the toolbar shows active state without stealing focus; stopping and
   tab close flush all queued bytes; the file contains raw terminal output and
   escape sequences but no separately captured keystroke or paste events.
2. Repeat session logging on a real SSH profile and end the remote shell while
   logging is active.
   Expected: disconnect and tab cleanup do not hang, crash, or truncate already
   queued output. Treat the transcript as sensitive because shell echo can
   include typed commands.
3. Select an unwritable destination, remove write access while logging, and—on
   suitably slow removable storage—produce sustained output faster than it can
   be written.
   Expected: start/write failures are visible and localized; terminal rendering
   remains responsive; queue exhaustion changes the save icon to warning color
   and its tooltip reports a non-zero dropped-byte count.
4. Export a script library, inspect its JSON, import it into a clean data mode,
   then import it again into the same library.
   Expected: names, commands, descriptions, shell scopes, and timestamps survive;
   no secret or history data appears; import never runs a command; identifier
   collisions create unique additional entries without overwriting existing
   scripts.
5. Operate session logging and script import/export from the command palette,
   toolbar, more menu, and keyboard-only navigation in English and Simplified
   Chinese, light and dark themes, and 100–200% DPI.
   Expected: focus returns predictably after file dialogs, accessible names are
   meaningful, labels fit, and no control is clipped or pointer-only.
6. Start a large upload and download, exit ztermy before completion, then start
   it again without opening a terminal.
   Expected: no host connection starts automatically; the transfer center shows
   each saved operation as interrupted with an actionable Retry state; the
   journal contains paths and transfer metadata but no credential or terminal
   content.
7. Unlock the required vault if needed and explicitly retry an interrupted
   transfer. Also test a missing profile, rejected credential, unavailable local
   destination, and unwritable remote directory.
   Expected: retry restarts from the beginning, a stale temporary upload created
   by the same task is removed, successful/cancelled entries leave the recovery
   journal, and each failure presents a localized corrective action without
   exposing sensitive data.

## V1.9 release-candidate accessibility and lifetime

Status: `PENDING`

1. In Windows Settings, enable a high-contrast theme while ztermy is open, then
   switch among at least two Windows contrast themes and finally turn the mode
   off.
   Expected: every ztermy surface immediately becomes opaque and uses the chosen
   Windows background, text, highlight, and highlight-text colors; Acrylic,
   Transparent, Mica, and Mica Alt are suspended; animations stop; visible focus
   remains clear. Turning high contrast off restores the saved appearance
   without changing Settings.
2. With high contrast enabled, operate Hosts, the profile editor, Settings,
   command palette, local terminal, terminal search, history/scripts/composer,
   SFTP, transfer center, tooltips, menus, confirmations, and empty/error states
   using keyboard only and Narrator.
   Expected: no text disappears into a selected/hovered surface, borders and
   focus are distinguishable, accessible names describe icon-only controls, and
   modal focus returns to the invoking control.
3. Repeat the complete route in English and Simplified Chinese at 100%, 125%,
   150%, 175%, and 200% DPI, including narrow width and maximized/Snap layouts.
   Expected: labels and translated dialogs fit or elide intentionally; hit
   targets, terminal cell geometry, resize cursors, and Snap Layout hover align
   with rendered pixels.
4. Open and close ztermy repeatedly with no terminal, a local terminal, a live
   SSH terminal, an active session log, open SFTP, and queued/running transfers.
   Expected: the window and process exit promptly; no invisible `ztermy.exe`,
   `pwsh.exe`, CRT assertion, heap dialog, lost recovery entry, or truncated
   flushed log remains. Interrupted transfers reappear only as explicit Retry
   items on the next start.
5. Upgrade an installed build and replace a portable build while retaining their
   respective data directories, then uninstall the MSI.
   Expected: supported settings/profile/workspace/recovery data migrates without
   auto-connect or command execution; installed binaries and shortcuts are
   removed; user profiles, logs, and credentials are retained by design and are
   documented rather than silently deleted.

## Evidence template

```text
Date:
Commit/build:
Windows/Qt/DPI/display setup:
Authentication/profile:
Cases executed:
Result: PASS | FAIL | PARTIAL
Observed differences, logs, screenshots:
```
