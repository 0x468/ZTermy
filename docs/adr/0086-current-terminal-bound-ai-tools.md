# ADR 0086: Bind native AI tools to the sidebar's current terminal

Status: accepted

Date: 2026-08-15

## Context

Each ztermy AI sidebar belongs to one terminal tab. Earlier milestones exposed
`list_sessions`, `read_multi_session_status`, and model-visible
`session_id`/`session_generation` arguments. That contract made the model solve
an application-routing problem, enlarged every schema, and suggested a
cross-terminal product that ztermy does not intend to provide.

A provider turn can still outlive a focus change, tab switch, reconnect, or tab
closure. Removing model-visible routing must therefore not weaken the internal
lifetime check that prevents a delayed tool call from reaching a different
terminal generation.

## Decision

- Every native AI tool advertised by one sidebar is implicitly scoped to the
  terminal tab that started the turn.
- Remove `list_sessions` and `read_multi_session_status`. No native tool may
  discover or select another ztermy terminal.
- Remove `session_id` and `session_generation` from provider-visible native
  tool schemas, arguments, results, serialized context, and system-prompt
  instructions.
- At turn start, ztermy captures an internal immutable target consisting of the
  tab ID and reconnect generation. Parsers receive that target from the host,
  not from provider JSON.
- Live reads, waits, writes, SFTP operations, and completion callbacks re-check
  the captured generation. A tab closure or reconnect returns `scope_changed`;
  it never retargets to the currently focused tab.
- Explicit terminal block/selection attachments and Agent-generated command
  results remain conversation context. Unrelated user terminal activity is not
  silently imported merely because it occurred in the same tab.
- External MCP resources keep their own reviewed identifiers. This decision
  concerns ztermy-owned terminal tools only.
- No compatibility aliases are retained for the removed tools or arguments.

## Consequences

- Provider tool calls describe work in the current terminal instead of
  application topology.
- The visible contract matches the product UI: one sidebar, one conversation,
  one terminal.
- Internal tab identity and reconnect generation remain correctness guards but
  are no longer prompt surface or model output.
- Switching tabs while a turn runs does not redirect the turn. Reconnecting the
  owning tab invalidates outstanding live work.
- Existing tests and evaluation fixtures must use targetless native schemas and
  separately assert host-side scope invalidation.

