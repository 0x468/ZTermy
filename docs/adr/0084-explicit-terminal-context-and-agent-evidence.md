# ADR 0084: Explicit terminal context and retained Agent evidence

Status: Accepted

Date: 2026-08-13

## Context

The first AI panel treated a recent terminal frame as convenient ambient
context. That makes an SSH assistant unpredictable: terminal output produced by
the user for an unrelated task can enter a later model request, while output
from an Agent tool can disappear when the viewport advances.

Terminal-native reference products instead distinguish user-attached terminal
evidence from evidence generated inside an Agent conversation.

## Decision

- `aiAutomaticContext` defaults to false and schema 17 migrates older settings
  to that default.
- Normal requests contain the visible conversation plus retained Agent
  evidence, not ambient terminal state.
- A user can explicitly attach the current selection or the last 1, 3, or 5
  completed semantic command blocks.
- Explicit attachments are consumed by the next request and then retained as
  bounded hidden evidence for follow-up turns.
- Agent tool arguments and bounded results are retained as hidden evidence.
- Hidden evidence persists with encrypted conversation history using the
  `evidence` role, but is not rendered as a chat message and does not inflate
  visible history counts or previews.
- Encrypted conversation history is enabled by default when the active vault is
  persistent. Schema 18 migrates the pre-release opt-in default; session-only or
  locked portable storage still disables durable history explicitly.
- Restoring history never restores permissions, pending calls, budgets, write
  leases, or executable state.

## Consequences

The model receives less accidental noise and more reliable task evidence.
Conversation storage now contains a third, non-visual role. Provider requests
may contain bounded evidence messages that are not visible chat bubbles. The UI
must offer explicit attachment affordances and make attached context
inspectable.
