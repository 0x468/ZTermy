# ADR 0096: Task-first AI surface

Status: Accepted

Date: 2026-08-21

## Context

ztermy is a personal terminal for technical users. The AI settings page had
grown a dedicated privacy-diagnostics card that exposed endpoint-scope labels,
redaction counters, runtime request counts, and an MCP-boundary summary. Those
values are useful to tests and support diagnostics, but they are not settings
and do not help a user choose a provider, model, action mode, context, or
conversation behavior.

The card also contradicted the repository product boundary: necessary safety
mechanisms should remain transparent and contextual instead of turning
internal architecture into concepts the user must manage.

Current terminal products keep their primary AI surface task-first:

- Wave AI presents chat, a workspace-context toggle, attachments, provider
  configuration, and tool actions rather than a permanent security dashboard;
- Warp exposes attached terminal context and action approvals at the point of
  use;
- the locally reviewed NetCatty settings organize AI around providers, model
  behavior, tools, and user-facing controls.

References:

- <https://docs.waveterm.dev/waveai>
- <https://docs.warp.dev/agent-platform/local-agents/agent-context>
- <https://docs.warp.dev/agent-platform/capabilities/full-terminal-use>

## Decision

- Remove the privacy-diagnostics card from the user-facing AI settings page.
- Keep provider, API key, model discovery, reasoning, action mode, optional
  context, history, debug trace, tools, and skills as direct settings.
- Keep internal diagnostic counters and automated contracts available to code,
  tests, and support tooling; hiding the card does not weaken any runtime
  boundary.
- Keep detailed request and response inspection in the explicit AI debug trace
  instead of duplicating it as an always-visible settings dashboard.
- Describe model scope as the terminal that owns the sidebar. Provider-visible
  prompts and tool schemas must not introduce another terminal or a selectable
  session abstraction.

## Consequences

- The AI page opens directly on settings users can act on.
- Runtime protection, redaction, bounded context, approval policy, and MCP
  isolation continue to operate without extra setup or explanatory clutter.
- Diagnostics remain testable without becoming product navigation.
- Future AI settings must earn their place through a user decision or workflow;
  implementation counters and trust-boundary summaries belong in logs, tests,
  or a deliberately requested support report.

