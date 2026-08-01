# V1.3 localization acceptance

Never translate or capture real credentials, private-key contents, terminal
input/output, hostnames, usernames, paths, or other user-owned data.

## Acceptance status

Status: accepted by the owner on 2026-08-02; MSI ICE revalidation is deferred
until the local Windows Installer validation environment is healthy.

Date: 2026-08-01

## Requirement coverage

| Requirement | Evidence |
| --- | --- |
| Global language and font preferences | Settings schema 7 stores `system`, `en`, or `zh_CN`, the optional global UI font, terminal font filtering, and terminal ligatures; schema 1–6 migration tests preserve every older settings document and add safe font defaults. |
| System fallback | `LocalizationManager` tests cover Simplified Chinese for mainland China and Singapore, and canonical English for Traditional Chinese and unsupported locales. Explicit English and Simplified Chinese always win. |
| First-frame and live translation | The translator is applied before the first QML component. Real-window checks switched System → English → 简体中文 without restart, closing Settings, or disturbing the current page. |
| Stable settings state | Enumeration controls use stable token models with independently translated labels. Real-window checks confirmed the language selector retained its explicit value and Appearance remained System / ztermy / Acrylic / 0% after a live language change. |
| Existing terminal sessions | Language changes rebuild application-owned tab status text from the current local/SSH state while preserving the session, snapshot, user output, title data, and connection. The AppController test covers the local connected-state refresh. |
| Complete application surface | All ztermy-owned strings in the QML module and critical C++ UI boundaries use Qt translation APIs. Accessibility names, validation, confirmations, empty states, Hosts, Settings, font pickers, terminal UI, host-key prompts, and credential flows are included. |
| Translation boundary | The catalog excludes remote output, terminal input, saved profile values, hostnames, usernames, paths, protocol tokens, logs, secrets, and private-key content. The source gate checks the critical boundaries and no secret-bearing data is introduced into the catalog. |
| Font discovery and shaping | `FontCatalog` tests cover system UI fallback, installed families, and monospaced filtering. Real-window checks confirm searchable UI/terminal pickers, monospaced-only default filtering, the all-font opt-in, OpenType capability messaging, and keyboard focus restoration. Terminal runs break at IME, style, selection, wide-cell, display-column, and visible-cursor boundaries. |
| Release packaging | CMake compiles 421 finished translations into `ztermy_zh_CN.qm` and embeds the resource in the statically linked executable. The portable archive contains only its directory, `portable.flag`, and `ztermy.exe`. |

## Automated evidence

The following gates passed against the V1.3 worktree:

- MSVC Dynamic Debug build and CTest: 25/25.
- MSVC Static Release build and CTest: 25/25.
- C++ formatting and clang-tidy with warnings as errors: 56 translation units.
- QML formatting and qmllint: 22 QML files.
- Qt Linguist freshness and completeness: 421 source texts, 421 finished
  Simplified Chinese translations, 0 new, unfinished, obsolete, vanished, or
  empty entries.
- Placeholder parity and QML/C++ localization call-site scans.
- Native-window smoke including compact/regular layout and keyboard routes.
- WiX per-user MSI contract and complete portable/MSI release-bundle assembly.
- `git diff --check`.

Real-window Debug evidence on Chinese Windows 11 at 1194 × 798 covered the
Chinese System fallback, explicit English, explicit Simplified Chinese,
immediate retranslation, translated accessibility tree, persisted display,
stable non-language setting drafts, and regular-width CJK layout. No clipping,
overlap, empty labels, horizontal scrolling, QML warning, or session closure
was observed.

The final Dynamic Debug and Static Release real-window matrices passed all
seven gates: work-area behavior, Windows backdrop materials, native resizing,
100/125/150/200% DPI, responsive settings layout, keyboard routing, and
terminal rendering. The keyboard gate now checks both font pickers open into
their search fields and restore focus on Escape. A direct desktop inspection
confirmed that System UI font resolves to `Microsoft YaHei UI` on this machine,
the terminal picker initially contains fixed-pitch families, and enabling
**Show all installed fonts** exposes proportional families.

A clean extraction of the final Static Release portable archive was also
tested with its own empty data directory. System opened in Simplified Chinese;
an already-running local PowerShell session survived the switch to English and
its ztermy status changed from `本地 PowerShell 已连接` to
`Local PowerShell connected`; English persisted across a full restart; and the
test instance was returned to System before exit. No installed-mode settings or
credentials were read or modified by this check.

The same final portable executable was resized through the native window frame
to 764 × 604 while using System/Simplified Chinese. Hosts correctly switched to
its compact vertical flow, and Application, Appearance, Terminal, and Security
settings remained readable without clipped labels, overlapping actions, or
horizontal scrolling. The longer Security surface used its intended vertical
scroll path.

Static Release generated the current portable archive under
`build/msvc-static-release/package/portable` and the MSI at the Static Release
build root. The combined release-bundle assembly remains behind the pending ICE
gate. Current SHA-256 values are:

```text
1c96157d443d6f9868cea5bd8cfd0799a2f050c7311d58dd55666a93893b7aee  ztermy-0.1.0-windows-x64-portable.zip
fce9e9dc6ae73382351b3a562e8cc6f6820046f8f1d453021b67cb7cec5f0476  ztermy-0.1.0-windows-x64.msi
```

The MSI is generated successfully, but its final WiX ICE inspection is pending.
Two validation attempts on 2026-08-02 returned WiX WIX0217 for every ICE action:
Windows Installer reported that its service could not be accessed even while
`sc.exe query msiserver` reported the service as running. This is recorded as a
local validation-environment blocker rather than an application or MSI build
failure. The owner previously accepted installed-mode behavior and the slow
Windows/Sandbox Preparing phase; repeat ICE validation when the local service
environment is healthy.

## Owner-performed desktop checklist

Owner result: passed on 2026-08-02. The checklist remains below as the V1.3
regression contract for later releases.

1. Start the final Static Release portable build with **System** under Chinese
   Windows. Confirm Hosts and all four Settings categories use Simplified
   Chinese immediately.
2. Change to **English** without restarting. Confirm the current page remains
   open, focus remains usable, no terminal session is closed, and all visible
   application text changes to English.
3. Change to **简体中文** and repeat the same continuity check.
4. Keep a local terminal open while switching both directions. Return to it
   and confirm the session remains alive, terminal output is unchanged, and
   the ztermy status text uses the new language.
5. Restart after English, 简体中文, and System; confirm each preference persists
   and System resolves back to Simplified Chinese on this computer.
6. Review Hosts, New/Edit Profile, all Settings categories, terminal toolbar,
   search, paste confirmation, host-key prompt, connection failures, empty
   states, and portable-vault flows in both languages.
7. Repeat at the minimum supported window size and a regular/maximized size in
   Dark and Light themes. Confirm no clipped text, overlapping actions, empty
   labels, unexpected horizontal scrolling, or broken keyboard focus.
8. Confirm terminal output, saved host/profile values, usernames, paths, and
   protocol identifiers remain byte-for-byte unchanged while the UI language
   changes.
9. Run both the installed and portable release artifacts and confirm the same
   language behavior and persistence rules. Windows Installer's own Preparing
   UI is not part of the ztermy application catalog.
10. Under **Appearance**, confirm System UI font shows the Windows-resolved
    family; search for and apply a custom UI font, verify English and Chinese
    remain readable, then return to System default and restart.
11. Under **Terminal**, open the font picker and confirm the initial list is
    monospaced. Search for Cascadia/Fira/Consolas, then enable **Show all
    installed fonts** and confirm proportional fonts appear with a warning when
    selected.
12. With a ligature-capable font such as Cascadia Code or Fira Code, compare
    `=> == != <= >= -> === !==` with programming ligatures on and off. Move the
    cursor through the sequences and confirm the active cell stays clear and
    addressable.

Expected: System/English/简体中文 switch live and persist globally; every
ztermy-owned UI string is complete in both languages; user-owned and protocol
data are never translated; layout, keyboard, accessibility, security, and
terminal behavior remain intact.
