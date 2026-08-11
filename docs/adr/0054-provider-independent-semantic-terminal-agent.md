# ADR 0054: Provider-independent semantic terminal agent

Status: accepted, 2026-08-11

## Context

V2.14 provides a stable native Windows 11 SSH workspace with bounded terminal,
SSH, SFTP, forwarding, telemetry, scripts, notes, credentials, persistence, and
shutdown behavior. The owner selected AI integration as the V3 direction.

Reference research shows two distinct implementation strategies:

- buffer-oriented assistants read recent rendered scrollback and send commands;
- semantic agents model commands, outputs, lifecycle, live terminal frames,
  permissions, and control transfer as separate typed concepts.

Warp now publishes its client and confirms the second strategy, but its model
routing, streaming orchestration, and conversation state remain hosted. VS Code
also demonstrates that reliable command identity and completion require shell
integration rather than raw input reconstruction. ztermy must support cloud and
local models without introducing a web runtime or making one vendor's backend a
product dependency.

## Decision

Build V3 around four ztermy-owned boundaries:

1. a bounded semantic terminal layer (`CommandBlockStore` plus
   `TerminalFrameSource`) fed by interoperable and authenticated shell lifecycle
   events, with explicit degraded capability and output-coverage levels;
2. an `AiContextBroker` that selects, normalizes, redacts, deduplicates, bounds,
   previews, and serializes provenance-bearing context;
3. a provider-independent conversation and streaming protocol implemented by
   OpenAI Responses, explicitly compatible endpoints, and Ollama adapters over
   asynchronous Qt Network;
4. a typed native tool registry/executor whose scope, permissions, cancellation,
   audit metadata, and target generation are enforced outside the model.

QML owns presentation only. Ordered semantic capture either retains bytes or
marks a precise gap; derived provider/UI observation may coalesce stale updates.
Neither can backpressure terminal input, parsing, rendering, or shutdown.
Credentials use the existing installed/portable vault boundary. Conversations
are session-only initially; optional persistence requires a separately reviewed
encrypted store.

V3 exposes configurable professional autonomy. An explicit visible Run action
authorizes that exact action without a second generic warning. Model-initiated
actions follow capability-specific observer/confirmation/automatic policy.

## Consequences

- The first V3 milestone contains more foundation work than a chat-only feature,
  but later terminal, SFTP, telemetry, and MCP abilities share one trustworthy
  substrate.
- Model/provider changes do not alter QML, domain tools, terminal ownership, or
  permission policy.
- Shells without rich integration remain usable, but exact command and exit
  claims degrade explicitly.
- Interactive applications require a frame and control-handoff path in addition
  to command scrollback.
- Context/privacy behavior can be tested independently of a live model.
- ztermy avoids a mandatory hosted middle tier and can support local providers.
- Significant follow-up decisions—shell injection, encrypted transcripts,
  permission precedence, interactive frames, and MCP—receive focused ADRs.
- ADR 0055 defines ephemeral and explicit reversible shell integration. ADR 0056
  defines replay-safe tool dispatch, one-writer ownership, tool lifecycle,
  high-risk policy, and loop watchdogs.

## Rejected alternatives

- **Ship a chat sidebar over the last N lines:** fast visually, but unreliable for
  command attribution, exit status, background output, and alternate screen.
- **Copy Warp's client architecture or code:** violates the clean product/source
  boundary, encounters AGPL obligations, and still lacks Warp's hosted AI brain.
- **Make OpenAI JSON the domain model:** couples tools, persistence, QML, and
  tests to one provider and prevents clean local-provider support.
- **Use MCP as the internal tool bus:** adds protocol and process overhead to
  native operations and does not replace scope/permission ownership.
- **Route terminal input through AI:** threatens latency, privacy, IME behavior,
  and deterministic user control.
- **Store transcripts in Windows Credential Manager:** the store is appropriate
  for small credentials, not large versioned conversation payloads.
