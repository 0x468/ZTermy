# ztermy V1.5 scope

Status: accepted for implementation on 2026-08-02

## Goal

V1.5 establishes one native action and shortcut foundation for the whole
application. It adds a keyboard-first command palette and a NetCatty-aligned
Shortcuts settings category without duplicating action names, availability,
bindings, or dispatch rules in individual QML controls.

The milestone is an architectural prerequisite for future SFTP, split panes,
session restoration, and user-defined actions. V1.5 only exposes behavior that
ztermy actually implements.

## Included

1. Define every exposed application action once with a stable id, category,
   localized name and description, default Windows key sequence, palette
   visibility, repeat policy, and terminal-context requirement.
2. Keep registry, normalization, conflict detection, override persistence, and
   dispatch authorization in C++. QML owns presentation and maps authorized
   action requests to existing view-level operations.
3. Replace the fixed QML `Shortcut` objects for tab navigation, terminal find,
   new local terminal, and tab close with registry-driven shortcuts.
4. Add a themed command palette opened by `Ctrl+Shift+P` and a title-bar action.
   It supports incremental search, category and shortcut metadata, arrows,
   Home/End, Enter, Escape, pointer selection, and honest disabled states.
5. Add a Shortcuts category beside Application, Appearance, Terminal, and
   Security. It supports search, category grouping, click-to-record, unbind,
   per-action reset, reset all, and clear conflict feedback.
6. Store only validated overrides by stable action id. Defaults remain in code;
   localized labels are never persisted. Old settings migrate without losing
   other preferences, and unknown future action ids are ignored safely.
7. Bind only application-window shortcuts. Do not register OS-wide hotkeys and
   do not silently consume unbound terminal keystrokes.
8. Keep English and Simplified Chinese complete. Provide accessible names,
   keyboard focus indicators, themed tooltips/panels, reduced motion, and
   usable layouts from compact windows through 200% DPI.
9. Add domain, persistence, controller, and runtime UI evidence for action
   lookup, context gating, normalization, conflicts, recording, reset,
   localization, palette navigation, and terminal input preservation.

## Initial action surface

- Application: open command palette, open Hosts, open Settings, create a local
  terminal.
- Tabs: close the active terminal tab, activate the next tab, activate the
  previous tab.
- Terminal: find, show History, show Scripts, toggle Composer, hide the workbench,
  move the workbench side, and copy the active host address.

Actions without a safe and complete implementation remain absent rather than
appearing as placeholders.

## Product alignment

- Match NetCatty's compact Shortcuts settings hierarchy, grouped rows,
  record/reset affordances, and Windows-oriented labels.
- Use Windows Terminal's action-registry relationship: a stable action may
  appear in the command palette with or without a key binding, and unbinding a
  shortcut lets the underlying terminal receive that key again.
- Preserve ztermy's Settings work tab, native title bar, QML design system, and
  C++/Qt ownership boundaries. Do not copy NetCatty source, assets, icons,
  branding, or serial features.
- Use `Ctrl+Shift+P` for the command palette and keep terminal Find at
  `Ctrl+Shift+F`.

## Deferred

- System-wide hotkeys, quake/drop-down mode, multi-stroke chords, macros,
  argument-bearing user actions, imported key maps, and synchronization.
- Split-pane, SFTP, session logging, AI, serial, and recording commands until
  their backing features exist.
- A combined host/tab/action quick switcher. V1.5's command palette operates on
  registered actions; host and tab search can build on the same UI later.
- Multiple shortcut schemes and macOS glyphs while Windows 11 is the only
  supported platform.

## Acceptance

V1.5 is complete only when formatting, static analysis, Dynamic Debug and
Static Release builds and tests, QML/runtime smoke checks, portable packaging,
and owner-performed keyboard/mouse/manual acceptance pass. Evidence is recorded
under `docs/testing/V1_5_ACTIONS_SHORTCUTS.md`.
