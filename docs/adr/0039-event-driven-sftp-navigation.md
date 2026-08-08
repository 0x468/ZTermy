# ADR 0039: Event-driven SFTP navigation continuity

Status: accepted for V2.4

## Context

NetCatty connects SFTP navigation to the terminal's current directory and
offers list/tree browsing. ztermy previously had only a flat model and a
latest-request-wins directory command. Reusing that command for every expanded
node would cancel sibling loads, while deriving the current directory from
prompts or injected commands would be shell-specific and could pollute input.

The pinned libghostty-vt exposes the raw current directory reported by OSC 7,
OSC 9, and OSC 1337.

## Decision

- Store libghostty-vt's raw working-directory value in immutable terminal
  snapshots and normalize it at the application boundary.
- Accept only absolute normalized remote paths. Decode `file://` URIs but do
  not execute a command or scrape visible prompt text.
- Keep current-directory browsing latest-only and generation protected.
- Add independent queued tree-list commands whose results carry the current
  root generation. The hierarchical model owns expansion/loading/error state
  and presents a flat visible-row projection to QML.
- Persist `sftpViewMode` and `followTerminalDirectory` per saved profile in
  workspace schema v4; older schemas default to list view with follow off.
- Make following explicit, opt-in, and event driven.

## Consequences

- Bash, zsh, fish, PowerShell, or another shell can participate whenever it
  emits a supported OSC sequence; no shell adapter is required for navigation.
- Sibling tree nodes can load without cancelling each other, while a root
  navigation safely invalidates stale node results.
- Applications running inside the terminal can emit a current-directory OSC,
  as in other terminal emulators. The value only affects navigation after the
  user opens SFTP and explicitly locates or enables follow.
- A shell that emits no current-directory sequence receives an honest disabled
  state rather than guessed behavior.
