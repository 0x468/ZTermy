# ADR 0040: Bound background work and keep a last-known-good workspace

Status: accepted for V2.5

## Context

Lazy SFTP tree expansion can produce requests faster than one SSH/SFTP worker
can consume them. An unbounded FIFO makes memory and shutdown time depend on
pointer activity, and lets stale tree requests delay higher-value mutations.
Separately, atomic replacement prevents partial workspace files but cannot
recover from a fully written yet damaged payload or an interrupted external
edit.

## Decision

Each `SftpSession` owns a command-acceptance gate and a bounded queue for
background tree listing. Duplicate path/generation requests coalesce, a root
generation change removes stale tree work, and mutations are ordered before
tree discovery. Stop closes the gate before waking the worker and discards
queued commands. Refused operations report cancellation or capacity failure to
the existing asynchronous result surface.

`WorkspaceStateStore` retains atomic primary writes and also atomically records
the previous valid primary payload as `<workspace>.bak`. Loading may use that
backup when the primary is missing, unreadable, or invalid. A recognized newer
schema is an incompatibility, not corruption: neither load nor save silently
downgrades it.

## Consequences

- Memory and shutdown latency no longer grow without a defined bound from tree
  expansion alone.
- A mutation is not trapped behind a long exploratory backlog.
- Some background tree requests may be rejected under deliberate overload; the
  visible node can be retried later without corrupting the active root.
- The backup is one generation, not a journal, and recovery may restore the
  immediately preceding valid layout.
- Credentials remain in Windows Credential Manager or the encrypted portable
  vault; duplicating them into the workspace backup is forbidden.
