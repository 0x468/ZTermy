# V2.13 acceptance: scripts and local notes

## Automated evidence

Completed on 2026-08-10:

- C++ formatting, 46-file QML formatting/quality, and clang-tidy with warnings
  as errors passed;
- dynamic Debug and static Release each passed 60/60 serial CTest tests;
- script schema validation, quick-command migration, typed rendering, fixed
  targets, trigger chunking/timeouts, cancellation, terminal close,
  disconnect, and shutdown passed their focused tests;
- notes path containment, UTF-8/size/count/depth bounds, atomic writes,
  latest-generation search, and import/export passed their focused tests;
- 1217/1217 Chinese translations were complete;
- Release window gates passed for maximize/work area, DWM appearance, native
  resizing, 100%/125%/150%/200% DPI, compact/regular layouts, keyboard routes,
  large-output rendering/scrolling/split panes, and repeated ConPTY lifecycle;
- the authorized key-auth fixture passed authentication, terminal open/close,
  SFTP listing, host-key confirmation, remote history, telemetry, and the full
  real-window SSH/SFTP UI and transfer smoke path in Debug and static Release;
- structural MSI inspection passed for per-user LocalAppData installation, one
  executable, product icon, Start-menu shortcut, same-version upgrade, and
  uninstall folder removal. WiX ICE validation remains unavailable because the
  Windows Installer service is disabled on the build machine; it was skipped
  explicitly without enabling the service;
- the release bundle is
  `build/msvc-static-release/package/release/ztermy-0.2.13-windows-x64`:
  - portable ZIP SHA-256:
    `52b976884181207d35d903a0d97ac37d10a8ff4ae7cbda80864d3e8a238ca948`;
  - MSI SHA-256:
    `5a4424cb92b38e56b98dc71c57277b3e874db73836f5b6a38eb1d86ee24c1533`.

## Retained manual acceptance

These checks are intentionally retained for the owner and are not claimed as
completed by automated evidence.

1. Create, edit, reorder, export, import, and delete multiline scripts with all
   four non-secret variable types. Invalid variables and templates must explain
   the problem without losing edits.
2. Review a rendered script, change its explicit target, run it on local
   PowerShell and a real SSH tab, and verify immediate and literal-output-gated
   steps. Timeout, cancel, disconnect, and tab close must send no later step.
3. Record commands from the composer/script surfaces, review them, and save the
   recording as a script. Raw typing and password prompts must not appear.
4. Create nested English/Chinese Markdown notes, edit and save, search while
   typing rapidly, rename/move, import/export, and restart. Only the latest
   search generation may appear and no edit may be silently discarded.
5. Attempt traversal, absolute paths, oversized files, invalid UTF-8, unsupported
   files, and links escaping the notes root. Each must be rejected without
   modifying content outside the root.
6. Exercise scripts and notes from the left and right terminal workbench at
   compact/regular widths, keyboard-only, English/Chinese, light/dark, and 100%,
   125%, 150%, and 200% DPI.
