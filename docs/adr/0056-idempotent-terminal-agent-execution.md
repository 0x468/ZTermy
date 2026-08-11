# ADR 0056: Idempotent terminal-agent execution and session ownership

Status: accepted, 2026-08-11

## Context

Provider streams can reconnect, repeat events, or fail after a tool call was
accepted. Two AI conversations may also observe the same terminal while a long
command is active. A command gate alone does not prevent duplicate dispatch,
ambiguous ownership, repeated reads, or one conversation cancelling another's
wait.

Remote SSH side effects cannot be reliably contained by a local filesystem
sandbox. ztermy therefore needs execution identity, target integrity, bounded
autonomy, and explicit control ownership at the native tool boundary.

## Decision

### Replay-safe dispatch

Every tool dispatch has `(conversation_id, turn_id, tool_call_id)` plus tool
name, canonical argument hash, target session/generation, and state. An identical
replay joins the pending operation or receives its cached bounded result. Reuse
of the ID with different arguments fails closed. A side-effecting tool is never
blindly retried after an ambiguous provider/network failure.

The record is retained for the active conversation and encrypted-history
retention period. Restored unresolved writes are displayed as uncertain and are
never resumed automatically.

### Session ownership

A terminal session may have many read-only conversations and wait subscribers,
but at most one write/control owner. Another conversation must wait, request an
explicit transfer, or remain read-only. Closing/cancelling one observer removes
only its subscriptions. It cannot interrupt the owner's process.

### Tool lifecycle

- `run_command` returns a stable command/block handle when the owned input queue
  accepts the command.
- `read_command_output(after_cursor, max_bytes)` returns retained ordered
  segments, next cursor, lifecycle, and coverage. An expired cursor is explicit;
  a command is never re-executed to reconstruct output.
- `wait_command` is a cancellable lifecycle subscription with a deadline.
- `interrupt_command(soft)` sends the user-equivalent PTY interrupt. It does not
  claim OS process termination. Session close and any future signal/PID tool are
  separate capabilities.
- Disconnect or incomplete remote process knowledge returns `outcome_unknown`.

### Risk and autonomy

Direct visible Run authorizes that exact command/target once and does not receive
a duplicate generic warning. Model-initiated writes pass capability policy and a
deterministic high-risk overlay. High-risk actions ask even in automatic mode;
an advanced explicit grant may relax that only for the current session and exact
target. Deny rules and schema/scope failures always win.

Risk matching is defense in depth and reports its reason; it does not claim to
understand every shell program or pipeline.

### Loop watchdog

Initial per-turn defaults are 24 total tool calls, 8 side-effecting actions, 15
minutes, two transient provider retries, and three semantically identical reads
without a state-generation change. A limit stops the turn with a visible reason.
Continue creates a new bounded turn; the model cannot extend its own budget.

## Consequences

- At-most-once native dispatch is enforceable within the active/persisted
  conversation even when provider events repeat.
- Multiple AI views can safely observe one terminal without ambiguous writers.
- Long-running and interactive commands expose honest lifecycle and uncertainty
  rather than pretending every interrupt killed one known process.
- Professional autonomy remains available, while autonomous high-risk behavior
  and runaway loops have explicit, testable controls.

## Rejected alternatives

- **Trust provider tool-call IDs without argument hashing:** permits conflicting
  replay under one ID and leaves adapter bugs undetected.
- **Retry the whole turn after transport failure:** can duplicate remote side
  effects.
- **Let every conversation serialize writes through one queue:** ordering alone
  does not establish ownership or cancellation semantics.
- **Treat Ctrl+C as process kill:** inaccurate for ConPTY, SSH PTYs, shells,
  traps, remote process groups, and disconnected sessions.
- **Use prompts as the loop limit:** model instructions are not an enforcement
  boundary.
