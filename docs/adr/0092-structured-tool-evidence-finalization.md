# ADR 0092: Structured tool-evidence finalization

Status: Accepted

Date: 2026-08-15

## Context

An Agent can produce fluent final text even when a tool failed, timed out, was
cancelled, or never left an approval/running state. The conversation already
stores a bounded typed activity ledger for every tool call, but the final
assistant message previously exposed only the individual cards. A user could
therefore read a confident answer without noticing that one of its underlying
actions or observations did not complete.

This is a state-consistency problem, not a natural-language moderation problem.
OpenAI's documented Agent loop appends typed tool results before the next model
sample, and its App Server represents tool work as lifecycle items. Netcatty
similarly carries explicit tool result/error state into its grouped activity
presentation. Inferring success by scanning assistant prose would be brittle,
language-dependent, and impossible to make complete.

References:

- <https://openai.com/index/unrolling-the-codex-agent-loop/>
- <https://openai.com/index/unlocking-the-codex-harness/>
- `docs/reviews/netcatty-ai-comparison-2026-08.md`

## Decision

- Add a pure domain evaluator over `AiToolActivity`. It classifies the complete
  activity set as `none`, `verified`, `pending`, or `incomplete`, and records
  succeeded, pending, failed, and failed-side-effect counts.
- Treat `succeeded` as the only verified terminal state; `failed` and
  `cancelled` are incomplete; queued, awaiting-approval, running, executing,
  and unknown states remain pending. Failure takes precedence when a turn has
  mixed failed and pending work.
- Derive the verdict from the persisted activity ledger instead of storing a
  second mutable result. Restored conversations therefore reproduce the same
  verdict and cannot drift from their tool cards.
- Teach the model to review every tool result before its final answer and to
  state failed, cancelled, timed-out, or pending outcomes directly.
- When a completed assistant message has pending or incomplete evidence, show
  one compact themed notice below the answer. Selecting it expands the existing
  tool timeline. Do not show a notice for no-tool or fully verified turns.
- Do not rewrite, hide, or block the model's original answer, turn the verdict
  into a global application error, or add another confirmation step. The user
  retains the original response and can inspect the exact structured evidence.

## Consequences

- A polished but unsupported success claim remains visible for diagnosis, but
  it is no longer presented without the contradictory tool state beside it.
- The feature is part of ztermy's built-in provider-backed assistant. Earlier
  external-Agent adapters that once mapped into `AiToolActivity` were removed
  and must not be restored; ADR 0093 is the permanent product boundary.
- The evaluator is deterministic, provider-independent, language-independent,
  and cheap enough to derive in the item model.
- New tool states default to pending until deliberately classified, so an
  unrecognized lifecycle cannot silently appear verified.
