# ADR 0100: Response retry preserves completed tool evidence

- Status: Accepted
- Date: 2026-08-21

## Context

The built-in terminal assistant filters failed and cancelled assistant messages
out of provider history. That correctly prevents partial prose and incomplete
provider payloads from becoming durable conversation history. Tool results were
anchored after the same assistant message, however, and the live provider view
also filtered those results. Pressing **Retry** therefore resent the original
user request without showing the model that a command or another side-effecting
tool had already completed. Turn-local dispatch deduplication cannot prevent a
new provider turn from choosing the same action again.

Terminal AI products such as Warp retain commands executed in an Agent
conversation as part of that conversation and automatically include commands
run from the Agent view as context. ztermy needs the same continuity while
retaining its narrower one-sidebar/one-terminal ownership model.

Reference:

- <https://docs.warp.dev/agent-platform/capabilities/full-terminal-use>

## Decision

Failed and cancelled assistant prose remains excluded from provider replay.
Bounded tool evidence anchored to that message is preserved in chronological
order and is sent on a response retry. A transient provider-only continuation
message tells the model that the preceding evidence belongs to the interrupted
request, that completed tool calls have already executed, and that
side-effecting actions must not be repeated. This marker is neither displayed
as a user message nor persisted in conversation history.

Retry reuses the existing visible user message and its selected skills, web
search choice, and request mode. It does not append a duplicate user bubble.
The new provider turn remains bound to the sidebar's owning terminal and keeps
the ordinary action budget and permission policy. Exact retry dispatch still
uses a new provider turn; the retained evidence, not a cross-turn tool-call ID,
is the source of continuity.

## Consequences

- A provider failure after `run_command`, SFTP transfer enqueue, PTY input, or
  another completed action no longer erases the result before Retry.
- Retry can generate the missing answer or continue observation without
  blindly repeating a completed mutation.
- Failed partial prose remains non-authoritative and absent from future model
  context.
- Provider-message ordering, retry-marker behavior, transcript restore, and
  duplicate-prompt prevention require deterministic coverage.
