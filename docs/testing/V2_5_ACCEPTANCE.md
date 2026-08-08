# V2.5 acceptance — daily-use lifecycle closure

Status: implementation and automated acceptance complete on 2026-08-08;
owner daily-use soak pending

## Recorded automated evidence

- Dynamic Debug: all 42 registered tests passed, including the opt-in test
  executable in its safe default mode.
- Static Release: the integrated `ztermy_v2_automated_preflight` passed all 41
  non-real-host tests, C++ format, all 92 clang-tidy translation units with
  warnings as errors, 36 QML format/lint inputs, and all eight real-window
  gates.
- The real-host key suites, integrated SSH/SFTP UI smoke, and the focused
  20-cycle connect/disconnect/handle-growth test passed against the established
  owner-authorized test host.
- WiX ICE validation completed through the Windows Installer service. The
  decompiled contract passed with one executable, one product icon, per-user
  LocalAppData installation, Start-menu shortcut, same-version upgrade, and
  uninstall folder removal. ICE61, ICE69, and ICE91 remain informational
  warnings for the intentional current package shape.
- The final bundle is
  `build/msvc-static-release/package/release/ztermy-0.2.5-windows-x64` and
  contains exactly the portable ZIP, MSI, SHA-256 text manifest, and JSON
  release manifest.

## Automated release gates

Run from an x64 MSVC environment:

```powershell
cmake --build --preset msvc-static-release --target ztermy_v2_automated_preflight
```

Expected:

- formatting, clang-tidy, QML format/lint, and every non-real-host CTest pass;
- the lifecycle smoke completes eight sequential ConPTY start/close cycles,
  then closes three active sessions during application shutdown;
- SFTP tests prove the 128-request bound, duplicate/generation coalescing,
  mutation priority, and rejection after stop;
- workspace tests prove backup recovery and newer-schema preservation;
- the portable ZIP, per-user MSI, manifests, hashes, PE metadata, and icon
  contracts are produced under the versioned `0.2.5` release directory.

## Real-host focused check

Use the established test profile and run the opt-in password and key tests plus
the real-host UI smoke. Expected: terminal connection, SFTP home listing,
permission-error recovery, tree navigation, and tab close all complete without
a hang, crash, leaked prompt, or secret-bearing log entry.

## Owner manual matrix

1. Keep one SSH tab connected for at least 30 minutes while using the terminal,
   SFTP list/tree, bookmarks, upload/download cancellation, and Settings.
2. Expand a directory with many children rapidly, navigate elsewhere, then
   upload or create a file. The mutation remains responsive; stale nodes do not
   replace the current root.
3. Close the active tab while SFTP is loading, then close the application with
   two or more sessions connected. Expected: no freeze, runtime assertion, or
   delayed process left in Task Manager.
4. Relaunch after an ordinary close. Hosts, collapsed groups, workbench side,
   SFTP mode/bookmarks/follow preferences, theme, and language persist.
5. In an isolated copied data directory only, create one successful save (so a
   `.bak` exists), damage the primary workspace JSON, and relaunch. Expected:
   the last-known-good layout opens; credentials and terminal input never
   appear in either JSON file.
6. Install the MSI over the accepted `0.2.4` per-user build and also launch the
   extracted portable ZIP. Expected: settings migrate, installed and portable
   data stay separate, and uninstall removes program files without deleting
   user-owned data or credential-manager entries unexpectedly.

Record failures with the build type, exact action, relevant generic log lines,
and dump path. Never attach passwords, passphrases, private keys, or unredacted
terminal commands.
