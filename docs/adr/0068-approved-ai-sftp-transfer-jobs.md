# ADR 0068: Approved AI SFTP transfer jobs

Status: accepted

## Context

The assistant can inspect bounded remote files, but operations work also needs
an intentional path for moving an artifact. Direct provider-driven filesystem
I/O would bypass transfer conflict handling, cancellation, progress, recovery,
credential scope, and the user's transfer center.

## Decision

- Expose single-file `queue_sftp_upload` and `queue_sftp_download` actions with
  exact session generations and explicit absolute local and remote paths.
- Always show source and destination and require visible approval. Observer mode
  and unsaved quick connections are rejected. The action consumes a write
  budget but never claims PTY ownership or grants first-terminal-write access.
- Revalidate paths after approval. Uploads accept only existing regular
  non-symlink local files; downloads reject directory destinations; remote root
  is never a file target.
- Materialize an ordinary `TransferTask` through the existing `TransferManager`
  and saved-profile credential provider. Return the transfer identifier after
  enqueue, not after completion.
- Keep conflict prompts, host-key handling, progress, pause, cancellation,
  retry, recovery, temporary-file cleanup, and notifications in the existing
  transfer graph and transfer center.

## Consequences

- AI-initiated transfers behave like user-initiated transfers and remain
  inspectable and cancellable after the tool call completes.
- Approval never means overwrite approval; destination conflicts retain the
  transfer manager's separate explicit resolution step.
- Directory/batch transfer planning and symlink traversal remain outside the
  V3 0.3.2 AI surface.
