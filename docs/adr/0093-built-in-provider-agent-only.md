# ADR 0093: Built-in provider Agent only

Status: Accepted

Date: 2026-08-21

## Context

ztermy is a focused SSH terminal for technical users. Earlier V3 work added
experimental adapters for Codex App Server and OpenCode ACP and proposed more
external Agent integrations. Those adapters duplicated lifecycle, discovery,
configuration, persistence, permission, and UI concepts while diverting work
from the quality of the terminal assistant users actually interact with.

The product does not need to become a launcher or compatibility host for other
Agent harnesses. It needs one responsive, predictable assistant whose model is
selected through ztermy's existing provider configuration and whose tools are
bound to the current terminal.

## Decision

- ztermy maintains exactly one built-in, provider-backed terminal Agent.
- Codex, OpenCode, Claude Code, and other external Agent or harness runtimes
  must never be integrated, detected, launched, bridged, or presented as a
  selectable Agent.
- This is a permanent, version-independent product boundary. External Agent
  integration is rejected rather than postponed: it must not appear in any
  future roadmap, milestone, backlog, experimental spike, feature flag, plugin
  contract, dormant adapter, or compatibility layer.
- External products may be studied through public documentation and observable
  behavior to improve ztermy's interaction design. Their runtime protocols,
  account state, conversation ownership, and process lifecycle are not product
  dependencies.
- Provider selection, credentials, model discovery, reasoning support,
  conversation history, streaming, cancellation, Markdown rendering, and tool
  execution remain native ztermy capabilities.
- The assistant sidebar operates only on its owning terminal tab. It does not
  enumerate, select, or control other terminal sessions.
- No compatibility migration is required for the removed experimental Agent
  settings or external thread identifiers. Existing provider conversations
  remain valid when their native transcript can be read without those fields.

## Consequences

- Agent selectors, external CLI discovery, App Server/ACP processes, external
  thread identifiers, adapters, tests, and packaging rules are removed.
- The AI surface has fewer setup concepts and more room for provider, model,
  context, tool activity, response, and composer quality.
- Tool definitions and prompts can assume the current terminal as their only
  target, reducing ambiguity and preventing cross-terminal orchestration from
  entering the product.
- ADR 0091 and the experimental external-Agent implementation are superseded.
- Historical research and progress records that mention such integrations are
  non-normative records of discarded work. They cannot be used as justification
  to restore external Agent integration.
