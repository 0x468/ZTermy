# ADR 0094: ACP process and Session lifecycle

- Status: accepted
- Date: 2026-08-15

## Context

ADR 0093 fixed the bounded ACP v1 wire contract and the one-terminal ownership
boundary. A usable external Agent also needs a non-blocking subprocess,
deterministic Session lifecycle, cancellation that cannot leave the UI stuck,
and typed handling for streamed text, public thought, tool activity, and
context usage. These concerns must be proven without a real provider account or
network before terminal side effects are connected.

ACP keeps the original `session/prompt` request open for the whole turn. The
Agent streams `session/update` notifications and finally answers the original
request with a stop reason. `session/cancel` is a notification; cancellation is
complete only when that original prompt answers with `cancelled`.

## Decision

- Add an asynchronous `QProcess` ACP Client with explicit stopped, starting,
  initializing, opening-Session, ready, active-Prompt, and failed states. Stdout
  is bounded ACP NDJSON; stderr is drained and discarded rather than surfaced
  as model output or logged with terminal content.
- Initialization and Session setup share a ten-second deadline. Cooperative
  Prompt cancellation has a five-second deadline; an Agent that never closes
  the cancelled Prompt is terminated instead of leaving ztermy permanently in
  a busy state.
- One Client process owns exactly one ACP Session id. New and resumed Sessions
  are identity-checked, every `session/update` must name that Session, and no
  Session-list or cross-terminal routing API is introduced.
- Agent-to-Client requests are accepted only during the active Prompt. They are
  capped, bound to the owning Session when a Session id is present, and
  deduplicated by JSON-RPC id for the full Prompt before application dispatch.
  Duplicate ids receive an error and are never executed twice.
- Client request completion remains asynchronous and preserves ACP numeric or
  string ids. Unsupported capabilities receive JSON-RPC method-not-found rather
  than being reinterpreted as local file access.
- Map `agent_message_chunk` and `agent_thought_chunk` to ztermy text and public
  reasoning events. Map ACP tool calls to the existing typed tool-activity
  vocabulary while retaining bounded raw input/output. Keep ACP
  `usage_update.used/size` and optional cost as a separate context-usage type;
  it is not falsely reported as input/output token usage.
- Ignore unknown future Session metadata updates, but reject malformed known
  text, tool, and usage variants. This preserves forward compatibility without
  weakening known contracts.

## Consequences

- The process and mapping layers are deterministic and provider-independent;
  OpenCode or another ACP Agent can be connected later without changing the AI
  pane's conversation semantics.
- Terminal callbacks and permission requests are deliberately not executed by
  this infrastructure layer. The next application node must route them through
  the current tab's existing write lease, mode/rule decision, budgets, audit,
  cancellation, and reconnect generation.
- A failed or non-cooperative external Agent is isolated to its owning terminal
  Client. Other tabs and the built-in Agent remain unaffected.

## Evidence

- `acp-client`: new Session, resume, streaming, typed Client request, cooperative
  cancellation, foreign-Session rejection, and per-Prompt request deduplication.
- `acp-session-update-mapper`: assistant text, public thought, tool lifecycle,
  context usage/cost, malformed known updates, and forward-compatible metadata.
- <https://agentclientprotocol.com/protocol/v1/prompt-turn>
- <https://agentclientprotocol.com/protocol/v1/cancellation>
- <https://agentclientprotocol.com/protocol/v1/terminals>
- <https://agentclientprotocol.com/protocol/v1/tool-calls>
