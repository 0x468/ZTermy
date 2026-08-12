# ADR 0065: Immutable AI workspace target sets

Status: accepted

## Context

A provider turn can outlive focus changes, tab activation, and terminal output
updates. Rebuilding read snapshots at every tool call would let the same call
silently observe a different tab or reconnect generation. Operations diagnosis
also needs bounded comparison across the terminal panes the user placed in one
workspace.

## Decision

- At turn start, freeze the active terminal first and every other terminal tab
  in its explicit ztermy workspace. Tabs in another workspace are excluded.
- Copy session id, reconnect generation, terminal evidence, and bounded
  operations data into a shared immutable vector owned by the turn. Native read
  tools never rebuild it after focus or model activity.
- `list_sessions` describes only this target set. Add
  `read_multi_session_status` with an explicit array of up to 16 exact
  `(session_id, session_generation)` targets.
- Reject duplicate or malformed targets. Return one independent success or
  structured scope/not-found error for every well-formed requested target; one
  unavailable session does not erase evidence for the others.
- Live asynchronous tools still re-check their target generation immediately
  before completion because their data cannot be frozen without performing I/O.

## Consequences

- Focus changes cannot retarget a read tool, and a reconnect is visible as a
  stale scope instead of being treated as the old session.
- The target set is user-visible through the workspace layout and provider
  `list_sessions`; cross-workspace access is unavailable.
- Snapshot creation has a bounded copy cost proportional to panes in the active
  workspace. The existing operation and result ceilings still apply.
