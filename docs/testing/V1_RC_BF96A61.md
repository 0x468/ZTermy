# V1 release-candidate record: bf96a61

Status: automated preflight passed; immutable-candidate manual sign-off in progress

## Candidate identity

| Field | Value |
| --- | --- |
| Date | 2026-07-30 |
| Commit | `bf96a613703cb42e9037049a00e9365e80304800` |
| Version | `0.1.0` |
| Windows | Windows 11 Pro 10.0.26200, build 26200 |
| GPU | NVIDIA GeForce RTX 4060 Ti, driver 32.0.16.1074 |
| Additional display adapter | OrayIddDriver Device, driver 17.1.58.818 |
| Primary monitor | `2560x1440`, one physical display |
| Windows input methods | Simplified Chinese Microsoft IME and US keyboard |
| Release bundle | `build/msvc-static-release/package/release/ztermy-0.1.0-windows-x64` |
| Tester | Project owner |

The release bundle is immutable for this record. Rebuilding either artifact
creates a new candidate and requires a new record.

## Automated preflight

Command:

```powershell
$env:ZTERMY_QT_STATIC_ROOT = "D:\qt-self-built\qt-6.8.3-static"
cmake --preset msvc-static-release `
  -DZTERMY_WIX_ROOT="$PWD/build/tools/wix"
cmake --build --preset msvc-static-release `
  --target ztermy_v1_automated_preflight
```

Result: **PASS**

Evidence from the 2026-07-30 run:

- all 21 QML files matched `qmlformat` and passed `qmllint`;
- `clang-format --Werror` passed;
- all 44 project translation units passed LLVM clang-tidy with diagnostics
  treated as errors;
- all seven real-window V1 gates passed, including the 100%, 125%, 150%, and
  200% DPI matrix;
- all 20 CTest tests passed;
- the portable ZIP and MSI contract checks passed;
- the release bundle was regenerated with exactly four files.

Runtime captures are below
`build/msvc-static-release/test-data/v1-runtime-preflight`. They include Dark
and Light compact/regular Hosts and Settings views, four DPI captures, and the
completed 20,000-line terminal-render capture.

The representative captures were visually reviewed after the run. Dark and
Light regular views retained aligned title-bar controls, page insets, cards,
labels, fields, host actions, and semantic colors. The `500x360` Settings
capture correctly exposed a scrollable compact column instead of compressing
or overlapping controls. The two opacity controls are explicitly labelled
`Window background opacity` and `Terminal background opacity`. The terminal
capture contains line 20000, the unique completion marker, the returned
PowerShell prompt, and a visible history scrollbar without a stale resize
region.

The same Static RC also passed these opt-in real-host private-key gates against
the authorized `testkey@192.168.1.25` fixture with the independently trusted
fingerprint:

- explicit unknown-host confirmation followed by `id_ed25519`
  authentication;
- 120 queued input events at 5 ms intervals, with P95 below the 16 ms V1
  ceiling;
- 20 authenticated connect/disconnect cycles without a repeated trust prompt,
  failed worker shutdown, or process-handle growth beyond the test limit.

Only the private-key path was supplied. No passphrase, private-key content,
terminal input, or credential was written to the record.

## Manual portable and password pass

The project owner tested the exact Static RC after the artifact hashes below
were locked.

The interactive password gate ran against the authorized
`test@192.168.1.25` fixture. QtTest reported `3 passed, 0 failed, 0 skipped`
in 2877 ms using the MSVC 2022 static Release build and Qt 6.8.3. Password
input remained hidden, the shell authenticated successfully, no credential
appeared in output, and no PowerShell error dialog occurred.

The recorded portable ZIP was extracted into a new directory and completed
the prescribed route without a failure:

- it started without a missing DLL, Qt plugin, or runtime dialog;
- local PowerShell, Helix alternate screen, resize, exit, cursor restoration,
  CJK, IME insertion, selection, clipboard, paste confirmation, search, and
  scrollback behaved as expected;
- Acrylic, Transparent, Mica, Mica Alt, independent window/terminal opacity,
  ANSI explicit backgrounds, native maximize/restore, Snap Layouts, Win+Z,
  edge resize, and shutdown behaved as expected;
- configuration and diagnostics remained under the portable sibling `data`
  directory and did not contaminate installed-mode storage;
- no assertion, crash dialog, or lingering process was observed.

The exact recorded MSI then completed its per-user lifecycle:

- installation required no elevation and launched from the Start menu;
- Explorer, Start, taskbar, Alt+Tab, and Installed Apps displayed the expected
  ztermy identity and clear green terminal icon;
- a disposable password-authentication profile containing no password and a
  changed terminal-opacity setting survived a second run of the identical MSI;
- the same-version upgrade created no duplicate shortcut and retained profiles,
  host trust, settings, logs, and diagnostics;
- uninstall removed the executable, installation directory, shortcut,
  Installed Apps entry, and running process while intentionally preserving the
  per-user data directory;
- no assertion, installer error, PowerShell error dialog, or elevation prompt
  occurred.

The bundle was also copied into a fresh Windows Sandbox. Both independently
calculated hashes matched the two manifests. The portable candidate started
without developer tools or missing dependencies, ran local PowerShell,
visited the primary pages, resized and shut down normally, kept data beside
the portable executable, and did not contaminate installed-mode storage.

The Sandbox MSI route is not yet accepted. Windows Installer remained at
`Preparing to install` for an unusually long time. The same MSI completed its
host-machine lifecycle, and decompilation confirms it contains no executable
custom action, but a verbose Sandbox installation log is still required to
distinguish first-use Windows Installer initialization from a package defect.

## Artifact integrity

| Artifact | Bytes | Independently calculated SHA-256 |
| --- | ---: | --- |
| `ztermy-0.1.0-windows-x64-portable.zip` | 17,181,299 | `5a44122ffb13805f0d254868bbd280867b40c9337303af0a730681e4824e036d` |
| `ztermy-0.1.0-windows-x64.msi` | 14,393,344 | `4473bbdb33449a238feb73d47316929da27018041dc5f5eddaffa358a2e5c6ae` |

Both values exactly match `SHA256SUMS.txt` and
`release-manifest.json`. The manifest identifies ztermy `0.1.0`, Windows x64,
and SHA-256 schema version 1.

WiX validation produced only the three already reviewed ICE61, ICE69, and
ICE91 warnings. The automated MSI contract still passed per-user scope,
LocalAppData installation, product icon, direct Start-menu shortcut,
same-version upgrade, one non-empty executable payload, and uninstall folder
removal.

## Manual acceptance

Only results performed against the artifacts and hashes above count in this
table. Development Debug runs and earlier static candidates are useful
regression evidence but do not sign off this immutable candidate.

| Acceptance item | Result | Evidence / first failing step |
| --- | --- | --- |
| Mixed-DPI physical monitor transition | NOT RUN | Only one physical display is available. |
| Unsupported opacity/backdrop fallback | NOT RUN | No environment with unavailable Acrylic/Mica has been identified. |
| Narrow/regular mouse and keyboard visual route | NOT RUN | Automated captures passed; candidate still needs the complete manual route. |
| Dark/Light/System contrast and component states | NOT RUN | Automated Dark/Light captures passed; full manual component-state route remains. |
| Complete keyboard-only accessibility route | NOT RUN | Automated accessibility and Tab-order gate passed; full manual route remains. |
| Normal/snapped/maximized/mixed-DPI screenshots | NOT RUN | Automated normal and synthetic-DPI captures exist; required physical matrix is incomplete. |
| Windows artifact identity and icon clarity | PASS | Exact portable/MSI candidate showed the expected versioned ztermy identity and clear icon in Explorer and Windows shell surfaces. |
| ANSI/alternate screen/cursor/clear/resize | NOT RUN | Exact portable candidate passed ANSI background, Helix alternate-screen, resize, and cursor-restoration checks; explicit clear/cursor-movement route remains. |
| CJK/wide/combining/emoji/IME | NOT RUN | Exact portable candidate passed CJK and middle-of-line IME checks; combining-mark and emoji route remains. |
| Selection/copy/paste/search/scrollback | NOT RUN | Exact portable candidate passed the primary interaction route; reverse/rectangular selection and wrapped-search route remains. |
| Real-host password authentication | PASS | Static QtTest gate: 3 passed, 0 failed, hidden input, authenticated shell, 2877 ms. |
| Installed data survives upgrade and uninstall | PASS | Exact MSI completed per-user install, identical-MSI upgrade, persistence checks, and uninstall cleanup while retaining user data. |
| Clean Windows 11 release without developer tools | NOT RUN | Exact hashes and portable route passed in fresh Sandbox; MSI remained at `Preparing to install` and needs a verbose installation log. |

## Historical development evidence

The project owner manually accepted the corresponding behavior during V1
development, including:

- custom title-bar commands, Snap Layout hover, Win+Z, maximize work-area
  sizing, and native resize behavior;
- local PowerShell, SSH password and `id_ed25519` authentication against the
  authorized development fixture;
- host-key confirmation and saved-host workflows;
- alternate-screen use with Helix, resize, Unicode, CJK, IME composition,
  selection, paste confirmation, search, and scrollback;
- Dark/Light appearance, Acrylic, Transparent, Mica, Mica Alt, independent
  window and terminal background opacity, and persistence;
- portable and MSI startup in Windows Sandbox.

These results support release confidence but intentionally remain separate
from the immutable-candidate result table.

## Completion rule

Follow [V1_MANUAL_ACCEPTANCE.md](V1_MANUAL_ACCEPTANCE.md). Under the current
policy V1 is not formally accepted until every result-table row is `PASS`.
If unavailable physical hardware or an unsupported-DWM environment is to be
waived for this personal Windows 11 release, that policy change must be
recorded explicitly rather than silently treating `NOT RUN` as passing.
