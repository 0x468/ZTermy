# ADR 0095: Capability-filtered current-terminal AI tools

Status: Accepted

Date: 2026-08-21

## Context

ADR 0086 removed terminal enumeration and provider-visible routing identifiers,
but the provider request still advertised every native ztermy tool on every
turn. A local or disconnected terminal could therefore expose SFTP reads,
remote telemetry, port forwarding, and terminal writes that could only fail.
An SSH terminal whose SFTP browser was closed advertised the same file tools as
one with a live SFTP channel. Empty notes, scripts, and history collections also
produced unusable list/read pairs.

Current terminal products make terminal intelligence conditional on the active
terminal's real capabilities. Warp attaches Full Terminal Use to the active PTY
and its live terminal state. VS Code labels shell integration as none, basic, or
rich because command and completion features depend on what that shell can
actually report. Netcatty's current source similarly constructs tools from the
active AI scope and capability set. These are product observations only; no
third-party source is copied.

Advertising unavailable tools increases schema tokens, weakens model tool
selection, and produces avoidable failure cards. It also obscures ztermy's
product contract: one sidebar acts only on its owning terminal and the resources
that are currently attached to it.

## Decision

- Build the native tool catalog once at turn start from the owning terminal's
  immutable read snapshot plus live capability flags. The catalog remains fixed
  for that logical turn.
- Always expose the bounded current-terminal metadata and screen reads. Expose
  scrollback and frame tools only when their backing terminal objects exist.
- Expose semantic command-block reads only when blocks or a live output reader
  exist, and expose lifecycle waiting only when the shell integration supports
  it.
- Expose SFTP browse/read tools only while the owning terminal has a connected
  SFTP browser. Expose SFTP transfer actions only for a saved SSH profile with
  the transfer subsystem available.
- Expose shell history, scripts, notes, remote telemetry, and port-forwarding
  reads only when the corresponding bounded snapshot contains actionable data.
  Port-forwarding snapshots are filtered to the owning terminal's saved SSH
  profile; rules for other profiles never enter this turn.
- Expose terminal write tools only for a connected owning terminal and only in
  an action-capable mode. A disconnected terminal may still offer non-terminal
  actions such as saving a runbook when the mode permits them.
- Rename the provider-visible `read_session_info` tool to
  `read_terminal_info`. Do not retain an alias: ztermy has no backwards-
  compatibility requirement for internal V3 tool schemas, and “terminal”
  describes the product scope more accurately than “session”.
- Keep internal tab identity and reconnect generation as host-side correctness
  guards. They remain absent from every provider-visible schema and result.
- This policy must never introduce terminal enumeration or an external
  Agent/harness integration; ADR 0093 remains authoritative.

## Consequences

- The model receives fewer, more relevant schemas and is less likely to call a
  tool that can only fail in the current terminal.
- Opening SFTP, receiving telemetry, or reconnecting affects the next AI turn;
  it does not mutate the tool set while a provider response is in flight.
- Global user-owned notes and scripts remain usable from any terminal when they
  exist, but terminal/host operational data stays scoped to the owner tab and
  its saved profile.
- Capability filtering is independently unit-tested, including negative checks
  for `list_sessions`, provider-visible terminal IDs, unavailable SFTP, remote
  telemetry, terminal writes, and cross-profile port-forwarding data.

## Public references

- <https://docs.warp.dev/agent-platform/capabilities/full-terminal-use>
- <https://docs.warp.dev/terminal/blocks>
- <https://code.visualstudio.com/docs/terminal/shell-integration>
- <https://code.visualstudio.com/docs/agents/run/tools>
