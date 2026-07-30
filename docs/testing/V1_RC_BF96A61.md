# V1 release-candidate record: bf96a61

Status: accepted for V1 0.1.0

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

The Sandbox MSI remained at `Preparing to install` for an unusually long time
but eventually completed without intervention, missing dependencies, an
elevation prompt, rollback, or error dialog. It launched and used
installed-mode storage as expected. The owner accepted the delay for V1 as a
Sandbox/Windows Installer first-use observation. The same MSI completed its
host-machine lifecycle, and decompilation confirms it contains no executable
custom action. Installer latency may be revisited with a verbose MSI timing
log if it reproduces on a normal clean Windows installation.

The owner then completed the exact portable candidate's full terminal route.
ANSI foreground and explicit backgrounds, cursor movement, clear, Helix
alternate-screen restore, and repeated resize remained correct. Combining
marks, two-cell CJK, emoji, and middle-of-line Chinese IME composition retained
cell boundaries, shifted the suffix while uncommitted, and committed exactly
once. Linear, reverse, rectangular, and wide-character selection copied exact
text; multiline paste cancel/confirm, case-sensitive search, next/previous
wrapping, history anchoring, absolute scrollbar navigation, and return to the
live prompt all behaved as documented. No crash, assertion, stale frame,
duplicate input, half-cell selection, or forced scroll jump occurred.

The exact portable candidate also completed the final single-display UI route
at regular and minimum window sizes. Hosts, Settings, Terminal, profile
create/edit/copy/search/delete, settings discard/apply/reset, terminal tabs,
page scrolling, and compact-to-regular transitions remained aligned and
reachable without clipping or stale layout.

With the mouse set aside, Tab and Shift+Tab reached every tested title-bar,
tab, host, settings, dialog, search, and paste-confirmation action. Enter,
Space, Alt+Down, arrows, and Escape retained their expected control semantics;
focus stayed visible, dialogs contained focus, and each activation occurred
once. First-host-key confirmation was keyboard operable.

Dark, Light, and System were inspected across normal, hover, pressed, focused,
selected, disabled, validation, success, destructive, dialog, material, and
opacity states. System followed the Windows application color mode after
restart. Normal, snapped, maximized, and minimum-size single-display layouts
were visually accepted at the current physical scale. No manual screenshots
were saved, so the screenshot-evidence row remains open alongside the
unavailable physical mixed-DPI matrix.

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
| Mixed-DPI physical monitor transition | WAIVED | Owner-approved V1 waiver: only one physical display is available; automated 100%/125%/150%/200% DPI gates passed, but physical cross-monitor transition risk remains. |
| Unsupported opacity/backdrop fallback | WAIVED | Owner-approved V1 waiver: host and Sandbox both support Acrylic/Mica; invalid/unsupported DWM handling is automated, but a genuinely unsupported compositor remains untested. |
| Narrow/regular mouse and keyboard visual route | PASS | Exact portable candidate completed regular/minimum mouse workflows and compact transitions without clipping or overlap. |
| Dark/Light/System contrast and component states | PASS | Exact portable candidate passed full theme, semantic-state, material, opacity, and System-following inspection. |
| Complete keyboard-only accessibility route | PASS | Exact portable candidate completed the keyboard-only Hosts, Settings, dialogs, search, paste, and host-key route. |
| Normal/snapped/maximized/mixed-DPI screenshots | WAIVED | Owner-approved V1 waiver: single-display states were visually accepted and automated Dark/Light plus synthetic-DPI captures are retained; manual and physical mixed-DPI screenshots were not saved. |
| Windows artifact identity and icon clarity | PASS | Exact portable/MSI candidate showed the expected versioned ztermy identity and clear icon in Explorer and Windows shell surfaces. |
| ANSI/alternate screen/cursor/clear/resize | PASS | Exact portable candidate passed ANSI foreground/background, cursor movement, clear, Helix alternate-screen restore, and repeated resize. |
| CJK/wide/combining/emoji/IME | PASS | Exact portable candidate preserved combining, wide CJK, emoji, and uncommitted/committed Chinese IME behavior. |
| Selection/copy/paste/search/scrollback | PASS | Exact portable candidate passed linear/reverse/rectangular selection, keyboard paste, wrapped search, history anchoring, and scrollbar navigation. |
| Real-host password authentication | PASS | Static QtTest gate: 3 passed, 0 failed, hidden input, authenticated shell, 2877 ms. |
| Installed data survives upgrade and uninstall | PASS | Exact MSI completed per-user install, identical-MSI upgrade, persistence checks, and uninstall cleanup while retaining user data. |
| Clean Windows 11 release without developer tools | PASS | Exact hashes, portable mode, MSI install/launch, storage isolation, and shutdown passed in fresh Sandbox; first MSI preparation was slow but completed. |

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

## Completion decision

Follow [V1_MANUAL_ACCEPTANCE.md](V1_MANUAL_ACCEPTANCE.md). On 2026-07-30 the
project owner explicitly approved the three documented V1 waivers for physical
mixed-DPI transition, an actually unsupported DWM material environment, and
manual screenshot retention. All ten runnable acceptance rows are `PASS`; the
three unavailable/evidence rows are `WAIVED`; no row is `FAIL` or `NOT RUN`.

The immutable `bf96a61` Windows x64 artifacts are therefore accepted as ztermy
V1 `0.1.0`. The waivers remain visible risks for a future test-matrix expansion
and must not be described as tested behavior.
