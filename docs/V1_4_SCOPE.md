# ztermy V1.4 scope

Status: accepted for implementation on 2026-08-02

## Goal

V1.4 turns the terminal tab into a keyboard-first workbench. It adds a
NetCatty-aligned terminal toolbar and a resizable tool panel for browsing shell
history and reusable code snippets, while keeping execution, persistence,
SSH I/O, and secret-bearing data inside explicit native boundaries.

This milestone is foundational rather than cosmetic: the same command model,
panel routing, auxiliary SSH channel, and safety policy must be reusable by a
future command palette, shell integration, SFTP, session restoration, and
cross-platform ports.

## Included

1. Add a compact toolbar to every terminal surface. It exposes History,
   Scripts, Composer, Find, and panel visibility without moving application-level
   actions into the terminal viewport.
2. Add one workbench panel per terminal tab. It defaults to NetCatty's left
   split at approximately half of the usable terminal width, can be hidden or
   moved to the right, and is clamped between 320 logical pixels and the space
   required to keep a usable terminal viewport. Its last page, side, and width
   survive tab switching without forcing the panel open on a new session.
3. Keep terminal geometry correct while the panel animates, moves, or resizes.
   Terminal resize requests are coalesced and the terminal receives focus again
   after an action unless the user is editing or searching inside the panel.
4. Add a shell-history provider boundary with explicit loading, unsupported,
   empty, error, stale-request, and ready states.
5. Support read-only history discovery for remote Bash, Zsh, and Fish sessions
   through a bounded auxiliary exec channel on the already authenticated SSH
   connection. The interactive terminal channel remains responsive while the
   history request is in flight.
6. Support the default local PowerShell/PSReadLine history file on Windows.
   Respect a missing or disabled history file and report that state honestly.
7. Parse timestamped and non-timestamped Bash history, Zsh extended history,
   Fish history, and multiline PSReadLine records. Normalize ordering, remove
   adjacent duplicates, reject control-bearing entries, and cap results at
   1,000 entries and 2 MiB of source data.
8. Keep discovered shell history in memory only. Closing its terminal tab
   cancels any request and clears the cache. History commands, raw auxiliary
   output, and terminal input must never be logged.
9. Add a versioned global code-snippet store behind the Scripts surface. A snippet has an immutable id,
   name, command text, optional description, shell scope, timestamps, and a
   stable user-defined order. Code snippets are not SSH-profile appearance or
   credential data.
10. Provide create, edit, delete, reorder, search, insert, and run workflows.
    A history item can be saved as a code snippet. Insert sends text without a
    newline; Run sends the command to the active terminal followed by Enter.
11. Provide current-session and global history scopes with an entry count. The
    global view aggregates commands from open terminal tabs in memory only;
    it is not a second persistent shell-history database.
12. A vertically resizable bottom Composer sends with Enter and inserts a line
    break with Shift+Enter. Pinned snippet chips insert on click and send on
    Shift+click, with their full command and behavior disclosed on hover.
13. Make all controls reachable by keyboard. List navigation uses arrows,
    Home/End, Enter, and context actions; Escape returns focus predictably;
    destructive actions remain explicit. English and Simplified Chinese,
    accessibility names, focus indicators, reduced motion, and 100--200% DPI
    remain release gates.

## Product alignment

- Match NetCatty's terminal-local toolbar, compact icon tab strip, resizable
  panel, History search/refresh/scopes/count, Scripts vocabulary, Composer
  keyboard behavior, and per-entry Run/Insert/Save affordances.
- Preserve ztermy's native design system, existing top-level work tabs, and
  single custom terminal item. Do not copy NetCatty source, assets, icons,
  branding, or serial features.
- The code-snippet implementation intentionally stops short of NetCatty's script automation,
  recording, multi-host execution, AI, and SFTP panels. The V1.4 model must
  permit those later without presenting unfinished controls now.
- Remote telemetry, timestamps, session logging, highlighting, notes,
  recording, configuration-directory tracking, and alternate encodings are
  likewise hidden until their backing subsystems are production-ready.
- Per-tab Theme is not adopted: appearance remains one global ztermy policy.

## Deferred

- Windows OpenSSH remote PowerShell history, Cmd history, Nushell, Elvish, and
  shell history stored in custom non-exported paths.
- OSC 133 shell integration, prompt/command/output blocks, exit-code capture,
  working-directory capture, and automatic recording of every typed command.
- Parameterized command templates, folders/packages, team sharing, sync,
  multi-host execution, script runtime state, recording, and scheduled runs.
- User-customizable shortcut registry and command palette; V1.4 uses named
  actions and stable defaults so V1.5 can expose them.
- SFTP and serial tooling. Serial remains outside ztermy's product boundary.

## Acceptance

V1.4 is complete only when domain/store/parser tests, SSH auxiliary-channel
tests, controller tests, QML lint/format, clang-tidy, Dynamic Debug and Static
Release suites, real-window keyboard/mouse/DPI checks, real-host Bash history,
portable packaging, and owner-performed manual acceptance all pass. Evidence is
recorded under `docs/testing/V1_4_TERMINAL_WORKBENCH.md`.
