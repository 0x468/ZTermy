# ADR 0043: Resumable transfer and filename contract

## Status

Accepted for V2.8. This supersedes ADR 0030 only where ADR 0030 stated that
byte-range resume was not claimed; its explicit-retry and secret-handling rules
remain in force.

## Context

Restarting every interrupted large transfer wastes time and makes pause controls
misleading. Retaining arbitrary partials without validation can corrupt output,
while hiding reconnects at startup crosses credential and remote-side-effect
boundaries. Legacy SFTP servers may also expose path bytes in GB18030 rather than
UTF-8, so display-only encoding switches are insufficient.

## Decision

- A partial path is deterministic from the final destination and a SHA-256
  prefix of the stable task id. Downloads place it beside the destination;
  uploads place it beside the remote destination.
- Pause requests stop the owned worker and preserve the partial. Cancel requests
  stop it and remove the partial. Retryable failure preserves it. Dismissing a
  failed task triggers best-effort cleanup without blocking the GUI thread.
- Resume checks source size and modification metadata, then reconciles the
  journal offset with the actual partial length. Missing partials restart at
  zero; valid partial length wins over a lagging progress snapshot; impossible
  sizes or changed sources fail explicitly.
- libssh2 SFTP handles are opened in resume-safe mode and seeked with
  `libssh2_sftp_seek64`. Local files seek or append to the same offset.
- A completed download is committed from a same-directory partial with Windows
  replace semantics. A completed upload renames its remote partial to the final
  path after the handle closes.
- Recovery schema v2 stores status, byte counts, source modification time, and
  filename encoding. Paused tasks restore paused; tasks interrupted while
  running restore as failed `interrupted` and require Retry.
- UTF-8/GB18030 conversion decorates `SftpClient`, so every remote path operation
  crosses one encoding boundary. Raw credentials and path byte payloads are not
  logged.
- All transfer and cleanup threads are owned and joined by `TransferManager`.
  No detached worker may outlive application shutdown.

## Consequences

Pause and resume now describe actual transport behavior and survive normal
restart. Recovery remains explicit, source changes cannot silently append to an
old fragment, and cancellation/dismissal have a defined cleanup contract. The
tradeoff is extra persisted path metadata and one best-effort cleanup connection
when a failed upload is removed. Recursive directory resume and virtual remote
file drag remain separate designs.
