# ADR 0059: Bounded provider-neutral AI read tools

- Status: Accepted
- Date: 2026-08-12
- Milestone: 0.3.0

## Context

The semantic assistant needs to inspect terminal state beyond the context attached to the initial user message. The
inspection path must work across OpenAI Responses, OpenAI-compatible chat endpoints, and Ollama without allowing the
provider to select another terminal, bypass retained-data bounds, or turn terminal content into trusted instructions.

## Decision

- ztermy owns four read-only tools: `list_sessions`, `read_session_info`, `read_terminal`, and `read_command_block`.
- A terminal-scoped conversation advertises only immutable tool definitions and dispatches against the terminal tab
  that started the turn. `list_sessions` therefore returns the one session in that scope, not every open tab.
- Every session read includes the session generation. A reconnect or replacement invalidates stale reads rather than
  silently reading a different process or host generation.
- The dispatcher validates JSON objects and their allowed fields locally even when a provider claims strict schema
  support. Tool arguments are limited to 16 KiB; terminal ranges and command output retain the domain-level byte and
  line bounds.
- Tool results are machine-readable JSON. Terminal content, command output, paths, host labels, and replayed metadata
  are marked as untrusted evidence and never become system instructions.
- One logical model turn may perform at most 24 read calls. The runner accumulates fragmented tool arguments, invokes
  tools only after the provider has completed the call, and continues the same logical turn with provider-native tool
  result framing.
- OpenAI Responses continuations use `previous_response_id`; compatible chat and Ollama replay ztermy's bounded typed
  tool exchanges in their native message formats.
- Cancellation, malformed calls, missing response identity, scope changes, and limit failures end the turn with a
  typed error. A request that has exposed a tool call is not retried as an unobserved transport failure.
- No write or execution tool is advertised in 0.3.0.

## Consequences

- The model can gather more evidence without receiving direct access to terminal, SSH, or Qt objects.
- Provider-specific wire formats remain below the application-owned tool contract.
- Read tools can return partial or bounded evidence, and callers must preserve the associated coverage metadata.
- Multi-session inspection requires a later explicit scope and immutable target-set decision; it is not obtained by
  guessing another tab identifier.
