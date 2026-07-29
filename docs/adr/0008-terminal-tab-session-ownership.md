# ADR 0008: One owned session per terminal tab

Status: accepted

## Context

V1 requires multiple concurrent terminal tabs. The original controller owned
one local session and one SSH session and stopped one whenever the other
started. Creating a QML-only tab strip over those two objects would not preserve
independent terminal processes, scrollback, parser state, or connection state.

Creating one `TerminalItem` per tab would also make every background tab build
and upload scene-graph textures, increasing GPU memory and render work even
when a tab is hidden.

## Decision

`AppController` owns a collection of tab records. Each record owns exactly one
`LocalTerminalSession` or `SshTerminalSession`, plus its latest immutable
terminal snapshot and status.

Only the active tab's cached snapshot is bound to the single visible
`TerminalItem`. Background sessions continue their I/O and parser work and
replace their cached snapshot, but do not request a render. Switching tabs
publishes the cached snapshot immediately and resizes that session to the
visible viewport.

Host-key confirmation records the originating SSH tab and activates it before
showing the prompt. Confirmation or rejection is routed only to that session.
Closing a tab stops and destroys its worker threads before removing the record.

## Consequences

- Local and SSH tabs retain independent process, connection, parser, scrollback,
  selection, and resize state.
- Background output does not cause hidden texture uploads.
- The controller becomes responsible for tab identity, activation, shutdown,
  and cached presentation state.
- A single global modal host-key prompt is retained for V1; the originating tab
  is explicit, so a decision cannot be applied to a different connection.
- Per-tab renderer instances or split panes can be added later without changing
  session ownership.
