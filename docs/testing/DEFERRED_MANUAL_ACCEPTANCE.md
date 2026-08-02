# Deferred manual acceptance ledger

## Status and rules

The owner is temporarily unable to perform interactive validation. This file is
the authoritative ledger for runtime evidence that automation cannot honestly
replace. An item remains `PENDING` until the owner (or an explicitly authorized
operator) records the date, build/commit, environment, and result.

Current restrictions:

- do not use Computer Use until the owner explicitly allows it;
- do not operate NetCatty or ztermy through GUI automation;
- do not connect to the real SSH test host unless the owner explicitly allows it;
- command-line builds and synthetic/unit tests are allowed.

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

