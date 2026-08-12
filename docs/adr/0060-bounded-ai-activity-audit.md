# ADR 0060: bounded AI activity audit

- Status: Accepted
- Date: 2026-08-12

## Context

The terminal agent needs a user-visible lifecycle view and durable audit trail,
but tool arguments, terminal content, host identity, provider text, and raw
conversation identifiers may contain secrets. A conventional verbose request
log would violate ztermy's local-first privacy boundary. Updating the audit file
on the Qt Quick thread would also put persistence latency on the UI path.

## Decision

ztermy keeps one bounded activity card per `(conversation, tool call, tool)` and
updates that card through queued, approval, execution, success, failure, or
cancellation states.

The persisted record contains only:

- UTC timestamp;
- truncated SHA-256 references for the conversation and tool call;
- a validated owned tool-name token;
- lifecycle state and bounded result-code token;
- permission-mode token, session generation, side-effecting flag, and high-risk
  flag.

It never contains tool arguments, commands, PTY input/output, host/profile
identity, provider prompts/responses, or error prose. Records are capped at 500,
written through the last-known-good atomic file contract, and serialized on a
single owned worker queue. Export uses the same redacted document. Clear deletes
the model contents and persists an empty audit document.

The activity view is deliberately distinct from optional encrypted conversation
history. Enabling audit does not enable transcript persistence.

## Consequences

- Users can inspect, export, and delete a stable tool lifecycle trail without
  exposing terminal content.
- Replayed tool dispatches update the same card rather than manufacturing a
  second visible action.
- The audit can explain that an operation failed, was denied, or was cancelled,
  but cannot reconstruct the command or model reasoning; that limitation is an
  intentional privacy property.
- Installed and portable modes place the audit beside the existing settings
  data and use the same recovery behavior.
