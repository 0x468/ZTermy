# V1 manual acceptance

This runbook is the single manual sign-off entry point for the remaining V1
acceptance items. Detailed procedures remain in the linked topic documents.
Run it against one immutable static Release candidate; do not rebuild or
replace the MSI or portable ZIP between stages.

Before recording artifact hashes, run the complete automated preflight from an
x64 Visual Studio developer PowerShell:

```powershell
$env:ZTERMY_QT_STATIC_ROOT = "D:\qt-self-built\qt-6.8.3-static"
cmake --preset msvc-static-release `
  -DZTERMY_WIX_ROOT="$PWD/build/tools/wix"
cmake --build --preset msvc-static-release `
  --target ztermy_v1_automated_preflight
```

Expected: C++ and QML formatting, clang-tidy, and qmllint pass with diagnostics
treated as errors; every real-window gate runs serially; the complete CTest
suite passes; and the portable ZIP plus validated MSI are regenerated
successfully. Do not proceed with manual sign-off when this target fails.

## Evidence and result rules

Record this metadata before testing:

```text
Date:
Commit:
Windows edition/build:
GPU and driver:
Primary monitor resolution/scale:
Secondary monitor resolution/scale:
Windows IME:
MSI path and SHA-256:
Portable ZIP path and SHA-256:
Tester:
```

The current candidate paths are:

```text
build/msvc-static-release/ztermy-0.1.0-windows-x64.msi
build/msvc-static-release/package/portable/ztermy-0.1.0-windows-x64-portable.zip
```

Calculate their hashes without modifying either artifact:

```powershell
Get-FileHash .\build\msvc-static-release\ztermy-0.1.0-windows-x64.msi
Get-FileHash `
  .\build\msvc-static-release\package\portable\ztermy-0.1.0-windows-x64-portable.zip
```

Use only `PASS`, `FAIL`, or `NOT RUN` in the result table:

- `PASS` requires the complete procedure and its expected result.
- `FAIL` records the first failing step, visible symptom, environment metadata,
  and a screenshot or recording when practical.
- `NOT RUN` is required when hardware, an unsupported-DWM environment, network
  access, or a clean machine is unavailable. It never counts as acceptance.
- Record evidence paths, not secret content. Do not capture passwords,
  passphrases, private keys, terminal commands containing secrets, or
  credential-bearing logs.
- Preserve the newest local log and crash dump after a crash. Dumps can contain
  process memory and must remain private until reviewed.

## Stage A: real-host password authentication

Run this test directly from an interactive x64 Visual Studio developer
PowerShell. It deliberately refuses redirected input and does not run as part
of the ordinary CTest suite:

```powershell
$env:ZTERMY_TEST_SSH_PASSWORD_INTERACTIVE = "1"
$env:ZTERMY_TEST_SSH_HOST = "192.168.1.25"
$env:ZTERMY_TEST_SSH_USERNAME = "test"
$env:ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT = `
  "SHA256:GHfCnLRLvMPbxTvj1sdWqmZAPoX8GH10wCMGq5DRTio"
.\build\msvc-static-release\ztermy_ssh_terminal_session_tests.exe `
  authenticatesWithInteractivePasswordOnRealHost
Remove-Item Env:ZTERMY_TEST_SSH_PASSWORD_INTERACTIVE
Remove-Item Env:ZTERMY_TEST_SSH_HOST
Remove-Item Env:ZTERMY_TEST_SSH_USERNAME
Remove-Item Env:ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT
```

Expected: the password prompt does not echo input; the connection reaches a
usable shell; QtTest reports `3 passed, 0 failed`; no credential appears in
console output, configuration, status text, or logs. See
[SSH_TRANSPORT.md](SSH_TRANSPORT.md#interactive-password-authentication-gate).

## Stage B: terminal interaction

Start `build/msvc-static-release/ztermy.exe`, open a local PowerShell tab, and
complete these three routes:

1. **Terminal controls:** run `Get-ChildItem`, exercise an installed
   alternate-screen application, clear the screen, move the cursor, and resize
   while it is active. Colors, cursor placement, screen restoration, clear,
   and resize must remain correct. See
   [TERMINAL_SESSION.md](TERMINAL_SESSION.md#formatting-and-full-screen-behavior).
2. **Unicode and IME:** complete every step under
   [Unicode and IME](TERMINAL_SESSION.md#unicode-and-ime), including a
   combining-mark sample, emoji, a two-cell CJK cursor, and uncommitted Chinese
   composition inserted into the middle of `hello`. The suffix must move
   rather than be overwritten, and committed text must arrive exactly once.
3. **Selection, clipboard, search, and scrollback:** complete
   [Selection and clipboard](TERMINAL_SESSION.md#selection-and-clipboard),
   [Scrollback](TERMINAL_SESSION.md#scrollback), and
   [Search current screen and scrollback](TERMINAL_TABS_SEARCH.md#search-current-screen-and-scrollback).
   Linear/reverse/rectangular selection, single and multiline paste, Unicode
   search, wrapped matches, history anchoring, and return-to-prompt behavior
   must all match their documented expectations.

Any crash, assertion, duplicate input, corrupted adjacent cell, stuck
selection, or forced scroll jump fails the affected route.

## Stage C: UI, themes, keyboard, and screenshots

Use a disposable profile without a saved password or passphrase.

1. At the minimum window size and approximately `1120x800`, perform the complete
   mouse route in [Narrow layout](UI_SHELL.md#narrow-layout), then repeat the
   same workflow without the mouse. No control may clip, overlap, disappear,
   or move unexpectedly across the breakpoint.
2. In Dark and Light, inspect ordinary, hovered, pressed, focused, checked,
   disabled, validation-error, dialog, destructive, and successful states
   across Terminal, Hosts, and Settings. Apply System, change the Windows app
   color mode, restart ztermy, and confirm it follows the OS. Complete
   [Theme, opacity, and Windows backdrop](APPEARANCE_SETTINGS.md#theme-opacity-and-windows-backdrop)
   and [Host Vault theme states](APPEARANCE_SETTINGS.md#host-vault-theme-states).
3. Starting with the mouse set aside, complete
   [Keyboard navigation](UI_SHELL.md#keyboard-navigation),
   [Shared choice and boolean controls](UI_SHELL.md#shared-choice-and-boolean-controls),
   [Shared action buttons](UI_SHELL.md#shared-action-buttons),
   [Shared editable fields](UI_SHELL.md#shared-editable-fields), and
   [Confirmation dialogs](UI_SHELL.md#confirmation-dialogs). Every interactive
   action must be reachable, visibly focused, correctly named, and activate
   exactly once with the documented key.
4. Capture the same representative Terminal, Hosts, and Settings state while
   normal, snapped, maximized, minimum-width, and on each physical DPI scale.
   Accept only captures with sharp text/icons, consistent spacing and semantic
   color, visible focus, aligned hit targets, and no clipped edge, status text,
   field, popup, dialog, or title-bar control.

Name evidence consistently, for example
`v1-ui-dark-normal-100.png`, `v1-ui-light-snapped-150.png`, and
`v1-ui-system-maximized-200.png`.

## Stage D: Windows platform edge cases

### Physical mixed-DPI transition

This route requires two physical monitors using different scaling values, such
as 100% and 150% or 200%. Complete every step in
[DPI and monitor behavior](WINDOW_SHELL.md#dpi-and-monitor-behavior): drag the
restored window halfway across the boundary in both directions, inspect all
primary pages and a CJK terminal line on each monitor, then maximize, restore,
snap, and restore on both displays.

Expected: rendering repaints sharply; the window stays attached to the
pointer; logical proportions and hit targets remain aligned; work areas are
respected; no old-size frame, clipping, cumulative growth, or cumulative
shrinking appears. The automated scale matrix does not replace this physical
transition test.

### Unsupported opacity and backdrop fallback

Use a Windows 11 RDP session, VM, or other disposable environment where DWM
does not provide the requested Mica or Acrylic effect. If every available
environment supports both effects, record `NOT RUN`.

1. Start the static Release candidate with a disposable data directory.
2. Apply 75% opacity with None, then Mica, then Acrylic.
3. After each change, resize, snap, maximize, restore, open a dialog, and use
   the terminal and Settings controls.
4. Restart ztermy and confirm the saved selection can still be opened and
   changed back to None/100%.

Expected: an unavailable effect falls back to an ordinary readable background;
the window never becomes black, fully invisible, unclickable, or unstable.
Terminal cell colors and content do not change, all native window behavior
continues to work, and restart does not crash. A supported development machine
passing DWM readback is useful evidence but cannot mark this fallback route
`PASS`.

## Stage E: MSI lifecycle

Use the recorded MSI and complete
[Installer lifecycle](DISTRIBUTION.md#installer-lifecycle):

1. Before installing, confirm Explorer shows the green ztermy terminal icon
   for the portable `ztermy.exe`. The `.msi` file itself may retain Windows'
   generic package icon. Open the executable's **Properties > Details** and
   confirm product/file version `0.1.0`, product name `ztermy`, description
   `ztermy native SSH terminal`, and original filename `ztermy.exe`.
2. Install per-user without elevation and launch from the Start menu. Confirm
   the shortcut, taskbar, Alt+Tab, and Installed Apps entries use the same
   sharp ztermy icon rather than a generic executable icon.
3. Create a disposable non-secret profile, close ztermy, and run the exact same
   MSI again to exercise the V1 same-version upgrade.
4. Confirm profiles, host trust, settings, logs, and crash diagnostics survive.
5. Uninstall from Installed apps and inspect the Start menu, installation
   directory, Installed Apps entry, running processes, and per-user data.

Expected: upgrade preserves data; uninstall removes the executable, install
directory, shortcut, and Apps entry while deliberately preserving user data.
Windows identity fields are exact and all shell icon sizes remain recognizable,
without a white/black square or generic icon. No assertion, PowerShell error
dialog, UAC prompt, or orphaned process appears.

## Stage F: clean Windows 11 release

Use a separate clean Windows 11 x64 machine, a newly created VM, or Windows
Sandbox. It must not have Qt, OpenSSL, Visual Studio, CMake, Ninja, Zig, or
ztermy development directories on `PATH`. Do not install developer tools to
make the candidate run.

1. Copy only the recorded MSI and portable ZIP to the clean environment and
   verify both SHA-256 hashes.
2. Extract the portable ZIP into a new writable directory and launch
   `ztermy.exe`. Open a local PowerShell terminal, run
   `Write-Output ztermy-clean-portable`, resize, and close.
3. Confirm the sibling `data` directory contains portable profiles/logs/crash
   storage and no ztermy data appeared under `%APPDATA%` or `%LOCALAPPDATA%`.
4. Remove the extracted portable directory, install the MSI, and launch from
   the Start menu. Run `Write-Output ztermy-clean-installed`, open Hosts and
   Settings, resize, close, and uninstall.
5. Confirm installed mode used per-user application data, the installation
   artifacts were removed, and the preserved user-data directory contains no
   portable marker.

Expected: both release forms start without a missing-DLL/plugin dialog or
developer shell; local terminal, primary pages, native window controls, and
shutdown work; storage modes do not contaminate each other; no assertion,
crash dialog, or lingering process appears. Network SSH is optional in a clean
environment and must use only an explicitly authorized host.

## Result table

Copy this table into the V1 test record and attach evidence paths:

| Acceptance item | Result | Evidence / first failing step |
| --- | --- | --- |
| Mixed-DPI physical monitor transition | NOT RUN | |
| Unsupported opacity/backdrop fallback | NOT RUN | |
| Narrow/regular mouse and keyboard visual route | NOT RUN | |
| Dark/Light/System contrast and component states | NOT RUN | |
| Complete keyboard-only accessibility route | NOT RUN | |
| Normal/snapped/maximized/mixed-DPI screenshots | NOT RUN | |
| Windows artifact identity and icon clarity | NOT RUN | |
| ANSI/alternate screen/cursor/clear/resize | NOT RUN | |
| CJK/wide/combining/emoji/IME | NOT RUN | |
| Selection/copy/paste/search/scrollback | NOT RUN | |
| Real-host password authentication | NOT RUN | |
| Installed data survives upgrade and uninstall | NOT RUN | |
| Clean Windows 11 release without developer tools | NOT RUN | |

V1 manual acceptance is complete only when all twelve rows are `PASS`.
