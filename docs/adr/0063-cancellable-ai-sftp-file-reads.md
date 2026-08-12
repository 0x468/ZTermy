# ADR 0063: Cancellable AI SFTP file reads

Status: accepted

## Context

The operations assistant needs to inspect a remote file without blocking the
GUI thread, borrowing a live SFTP handle, silently creating another connection,
or allowing an active-tab change to retarget the request. Remote files can be
large, binary, replaced by links, or slow enough that closing a turn must cancel
the work promptly.

## Decision

- Expose `read_sftp_file` only for an exact terminal session id and reconnect
  generation whose SFTP browser is already connected. The tool does not
  auto-connect because host-key and credential work may require visible UI.
- Normalize an absolute remote path, reject root, traversal, missing entries,
  directories, and symbolic links, and read no more than 32 KiB plus one byte
  used only to report truncation.
- Return either strictly decoded UTF-8 text or Base64 selected by the caller.
  Other encodings are not guessed. Returned content is explicitly marked as
  untrusted evidence.
- Execute the operation on `SftpSession`'s worker thread. Every request owns a
  `stop_source`; pending and active reads can be cancelled without stopping the
  SFTP session. A turn, tab, or application shutdown cannot retain a live file
  read.
- Keep AI read request identifiers independent from directory-navigation
  request identifiers. Before completing a deferred tool, re-check the
  reconnect generation and return `scope_changed` after a reconnect.

## Consequences

- Provider callbacks never block on remote I/O or receive a live SFTP client.
- Reading is deliberately unavailable until the user has connected SFTP for
  that terminal. This is visible rather than silently opening another security
  boundary.
- Binary and large files are representable within the provider result limit,
  while complete arbitrary-file downloads continue through the existing
  transfer graph.
- Arbitrary directory listing, mutation, and transfer actions remain separate
  typed tools with their own permission contracts.
