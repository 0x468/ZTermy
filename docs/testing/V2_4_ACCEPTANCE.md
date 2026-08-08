# V2.4 manual acceptance

Use the static Release build. The checks below are read-only except where a
disposable directory is explicitly named. Never capture credentials, private
keys, terminal input, or secret-bearing commands.

## 1. Current-directory locate and follow

1. Connect to a saved SSH profile whose shell emits OSC 7/9/1337.
2. Change to two readable directories in the terminal.
3. Open SFTP and use **Open terminal working directory** after each change.
4. Enable **Follow terminal directory**, change directories again, then turn
   follow off and change once more.
5. Repeat after restart.

Expected: locate opens the normalized directory; follow changes only after the
shell reports a different path; disabling follow stops navigation; the setting
is retained only for that saved profile. A non-reporting shell leaves the
actions disabled with an explanatory tooltip.

## 2. Tree view

1. Switch to Tree view and expand two sibling directories.
2. Expand a nested directory, collapse/reopen its parent, and use Left/Right,
   Enter, arrows, Tab, and Shift+Tab without the mouse.
3. Expand an unreadable directory such as `/root` as an unprivileged user.
4. Navigate to another root path while an expansion is loading.
5. Restart and reopen the profile.

Expected: children load below their parent without replacing the active path;
sibling requests do not cancel each other; collapse/reopen reuses loaded
children; a node failure remains localized and announced; stale results do not
appear under a new root; list/tree preference persists per profile.

## 3. Layout, language, and lifecycle

1. Exercise list and tree views at workbench widths above and below 520 px in
   English and Simplified Chinese, light and dark themes.
2. Resize while several nodes are expanded and close the owning tab during a
   tree load.
3. Exit while SFTP is open.

Expected: the compact More menu exposes locate, follow, and view mode; labels
do not clip; indentation, focus, hover, loading, and error states remain clear;
shutdown has no hang, crash, assertion, or orphaned process.

## Automated preflight

Completed on 2026-08-08 against the `0.2.4` candidate:

- MSVC dynamic Debug configured and built successfully; CTest passed 42/42.
- MSVC static Release configured and built successfully; CTest passed 42/42.
- C++ formatting, QML formatting (36 files), QML lint, and clang-tidy passed.
- Translation freshness and completeness passed with 761/761 Simplified
  Chinese translations finished.
- Real-window smoke checks passed for the maximized work area, DWM appearance,
  native resize hit areas, 100%-200% DPI, responsive layout, keyboard routing,
  and terminal rendering under large output.
- The read-only real-host SSH/SFTP UI smoke passed with the configured key-auth
  test profile.
- The portable ZIP and per-user MSI were generated. WiX ICE validation,
  decompilation, and the installer contract passed; the release bundle contains
  SHA-256 manifests for both artifacts.

The checks above cover the implementation and its real-window integration. The
three manual sections remain the release acceptance for human visual judgment,
shell-specific OSC working-directory behavior, and unreadable remote directory
feedback.
