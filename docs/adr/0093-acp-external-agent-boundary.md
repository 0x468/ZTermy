# ADR 0093: ACP external-Agent boundary

- Status: accepted
- Date: 2026-08-15

## Context

ztermy already integrates its built-in Agent and Codex App Server through the
same conversation and current-terminal tool contracts. NetCatty, Zed, and the
broader Agent ecosystem also expose Claude, OpenCode, Gemini, Copilot, Cursor,
and other external harnesses. Implementing one private protocol per harness
would duplicate process, streaming, cancellation, permission, and persistence
logic.

The Agent Client Protocol (ACP) v1 is a stable JSON-RPC protocol for an Agent
subprocess. OpenCode 1.18.5 was exercised locally with `opencode acp`: it
negotiated protocol version 1, created a session, and returned model, mode, and
slash-command metadata. ACP can advertise terminal support independently from
local file-system support.

## Decision

- Add a bounded native C++ ACP v1 protocol layer. It owns NDJSON framing,
  JSON-RPC envelope validation, initialization, session creation/resume,
  prompting, cancellation, close, and Client responses. No Web, Node, Python,
  or Agent SDK runtime is embedded in ztermy.
- Start with the stable v1 schema. Unknown optional fields and update variants
  may be ignored deliberately; malformed envelopes, unsupported protocol
  versions, oversized messages, and identity mismatches fail visibly.
- ztermy will advertise ACP terminal capability but not ACP file-system
  capability. An external Agent must not gain implicit access to the user's
  local ztermy source tree or confuse it with a remote SSH file system.
- Every ACP process and Session belongs to one terminal tab and its reconnect
  generation. `terminal/*` callbacks will be serialized through that tab's
  existing action dispatcher, permission policy, write lease, budgets, audit,
  cancellation, and structured tool evidence. `terminal/create` does not
  create or select another ztermy tab.
- Do not expose ACP `session/list` as an AI tool or add cross-terminal
  discovery. A tab may retain only its own opaque external Session id for
  resume.
- External Agents own their own provider account, authentication, model, and
  native configuration. ztermy presents negotiated Session options instead of
  duplicating credentials in its Provider settings.
- Use a dedicated, content-free local working directory for ACP Session setup.
  Remote context is supplied through the owning terminal and explicit ztermy
  context, not by pointing the Agent at an unrelated local project.

## Consequences

- OpenCode is the first conformance target, while the transport is reusable for
  other ACP Agents without creating Agent-specific UI state machines.
- The protocol layer can be tested with deterministic fake Agents before any
  real model, account, or network is involved.
- Process lifecycle, Session-update mapping, terminal callbacks, permission
  requests, and user-visible Agent selection remain separate follow-up nodes;
  the stable wire boundary is fixed before those side effects are connected.
- ACP Agents may have capabilities ztermy intentionally does not advertise.
  Such capability reduction is visible and preferable to silently routing
  operations to the wrong machine.

## Evidence

- <https://agentclientprotocol.com/protocol/v1/initialization>
- <https://agentclientprotocol.com/protocol/v1/session-setup>
- <https://agentclientprotocol.com/protocol/v1/prompt-turn>
- <https://agentclientprotocol.com/protocol/v1/terminals>
- <https://agentclientprotocol.com/protocol/v1/tool-calls>
- <https://dev.opencode.ai/docs/acp/>
- <https://zed.dev/docs/ai/external-agents>
