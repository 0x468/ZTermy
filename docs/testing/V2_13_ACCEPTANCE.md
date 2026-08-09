# V2.13 acceptance: scripts and local notes

## Automated evidence

Recorded here as the implementation lands:

- script schema validation and quick-command migration;
- typed rendering, target fixation, trigger chunking/timeouts, cancellation,
  terminal close, disconnect, and shutdown;
- notes path containment, UTF-8/size/count/depth bounds, atomic writes,
  generation-cancelled search, and import/export;
- controller, QML, translation, dynamic Debug, static Release, runtime, and
  real-SSH gates.

## Retained manual acceptance

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

