# ADR 0064: Redacted AI script and note reads

Status: accepted

## Context

Script and note metadata is useful for discovery but insufficient for an
operations assistant that must explain an existing runbook. Their contents are
user-owned and may contain secrets or hostile instructions. Notes can be up to
2 MiB and must not be synchronously loaded or redacted on the GUI thread.

## Decision

- Add `read_script` for an exact session generation and script id. Its immutable
  turn snapshot contains bounded script steps and variable schema, but never
  variable default values.
- Add `read_note` for an exact session generation, normalized repository-relative
  Markdown path, and explicit byte limit no greater than 32 KiB.
- Apply the same built-in secret redactor used by AI context before returning
  either content type. Results report whether redaction occurred, remain marked
  as untrusted evidence, and retain the operations-result byte ceiling.
- Execute note loading, redaction, UTF-8 boundary truncation, and JSON assembly
  on the global worker pool. Only the bounded result crosses back to the GUI
  thread.
- Cancellation is logical for local note reads: it discards a late worker result
  without attempting to interrupt a bounded local filesystem call. Reconnect
  generation is checked again before deferred completion.

## Consequences

- A provider can inspect an explicitly selected script or note without gaining
  an unbounded repository export path.
- Inline secrets are reduced by deterministic redaction, but content is still
  presented as untrusted evidence rather than authoritative instructions.
- The application may finish a cancelled 2 MiB-or-smaller local read in the
  worker pool, but it cannot revive or complete a cancelled AI tool call.
