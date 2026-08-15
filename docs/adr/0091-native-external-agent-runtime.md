# ADR 0091: Native external Agent runtime

Status: Accepted

Date: 2026-08-15

## Context

Netcatty exposes its built-in Agent alongside Claude Code, Codex, Cursor,
Copilot CLI, Codebuddy, and OpenCode. Its external drivers preserve the same
terminal-scoped tool surface while delegating planning and conversation state
to each Agent SDK. ztermy needs comparable capability without embedding a Web
or Node runtime, scraping an interactive CLI screen, or allowing an external
process to bypass the current terminal's ownership and permission contracts.

OpenAI documents Codex App Server as the protocol intended for rich client
integrations. It uses newline-delimited JSON over stdio, exposes explicit
thread/turn lifecycle and cancellation, streams typed item notifications, and
can request client-executed dynamic tools. The separately documented SDKs are
TypeScript and Python wrappers and would add a runtime not otherwise needed by
the native Qt application.

The installed Codex CLI can generate its versioned App Server schema. Stable
schema generation intentionally filters experimental fields, so discovery must
use `generate-json-schema --experimental`. Protocol features can nevertheless
differ between installed releases; dynamic tools must never be assumed solely
because the process starts. The locally installed Codex CLI 0.146.0 exposes the
required dynamic-tool contracts in its experimental schema.

References:

- <https://developers.openai.com/codex/app-server>
- <https://developers.openai.com/codex/sdk>
- <https://developers.openai.com/codex/noninteractive>
- `docs/reviews/netcatty-ai-comparison-2026-08.md`

## Decision

- Keep the built-in ztermy Agent as the default. External Agents are optional
  runtimes, not model providers and not replacements for the existing Agent.
- Implement Codex first through a native C++/Qt App Server adapter over
  `QProcess` stdio. Do not embed Node, Python, a browser runtime, or parse the
  human-facing CLI output.
- Bind one external thread to the terminal tab whose sidebar created it. Do not
  expose session enumeration, cross-terminal routing, or a target selector.
- Start the external runtime with its own filesystem sandbox set to read-only
  and approvals disabled. All terminal mutations happen through client-executed
  ztermy dynamic tools, so the existing mode, reusable-rule, deduplication,
  budget, audit, cancellation, command-gate, and reconnect-generation checks
  remain authoritative.
- Probe the exact installed executable asynchronously: read its version, generate
  its bounded experimental schema, and require `thread/start.dynamicTools`, the
  dynamic-call request fields, and the dynamic-result response fields before
  exposing the Agent as available. An unsupported or mismatched Codex version
  is displayed as unavailable with a reason; it must not silently fall back to
  built-in shell access or to the built-in ztermy Agent.
- Keep the wire protocol bounded: 4 MiB aggregate receive buffer, 2 MiB per
  message, 1 MiB prompts, 256 KiB dynamic-tool results, 128 tools, strict JSON
  envelopes, absolute working directories, bounded UTF-8 identifiers and
  schemas.
- Do not expose an Agent selector until discovery, lifecycle, event mapping,
  dynamic-tool execution, cancellation, recovery, and packaging work end to
  end. A visible but inert option is not acceptable.
- Consider Claude and OpenCode adapters only when a stable official machine
  protocol can preserve the same host-owned contracts. Screen scraping remains
  out of scope.

## Consequences

- ztermy gains a native route to external Agent ecosystems while keeping its
  single-terminal product model and existing UI/event vocabulary.
- Codex owns planning, reasoning, and resumable thread state; ztermy owns the
  terminal, credentials, permissions, evidence, and tool execution.
- The protocol, asynchronous installation probe, process lifecycle, and typed
  event-to-conversation mapping are independently testable. Text, reasoning,
  tool/search activity, usage, cancellation, and failure share the built-in
  Agent's event vocabulary. Current-terminal tool dispatch is still required
  before UI availability can be claimed.
- Codex versions that lack dynamic-tool negotiation remain unavailable rather
  than receiving broader local-machine access.
