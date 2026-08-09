# V2.8 scope: SFTP and transfer workflow closure

Status: implemented for `0.2.8`

V2.8 closes the daily-use SFTP and transfer gaps retained after V2.7. The work
uses ztermy-owned Qt/C++ state and libssh2 I/O; NetCatty remains a product-flow
reference only.

## Included

- Uploads and downloads support real pause and byte-range resume. Pausing stops
  the worker, preserves the deterministic partial, records the exact progress,
  and releases its queue slot. Resume opens and seeks both endpoints.
- The versioned recovery journal persists paused state, byte counts, filename
  encoding, and source modification metadata. A crashed in-flight task remains
  an explicit `interrupted` retry; it never reconnects at startup.
- Resume validates source size/modification data and the actual partial length.
  A missing fragment restarts safely from zero; a valid fragment whose length is
  ahead of the last journal snapshot becomes the new trusted offset.
- Cancel removes the partial. Retryable transport failure retains it. Removing a
  failed record performs best-effort local or remote cleanup through an owned,
  stoppable worker.
- Completed downloads replace their destination through same-directory
  `MoveFileExW` commit semantics on Windows.
- The transfer center exposes aggregate pause/resume/cancel, per-task
  pause/resume/retry/cancel/dismiss, progress, copy path, open download folder,
  and clear-completed actions.
- A completed download row exports `text/uri-list` through Qt's native drag path
  so it can be dragged to Explorer. Upload drag-in remains supported.
- SFTP exposes sortable name/modified/size/type headers, configurable visible
  columns, directory-first order, and UTF-8/GB18030 filename encoding. These
  workflow choices persist per saved host in workspace schema v5.
- Filename encoding is applied at the `SftpClient` boundary for navigation,
  mutations, and transfers; it is not a cosmetic label.

## Bounds and exclusions

- Only regular-file transfer jobs are resumable. Recursive directory jobs need
  a separate job graph, conflict, and recovery model.
- Explorer drag-out is supported after a local download exists. Direct virtual
  drag of an undownloaded remote file would require a Windows `IDataObject` /
  `IStream` provider and is not claimed.
- UTF-8 and GB18030 are the only filename encodings. Unsupported or invalid
  conversions fail explicitly without logging path bytes or credentials.
- Paused transfers count as active user work but not as running queue slots.
- Recovery data contains paths and identifiers but never passwords, private-key
  content, terminal input, or vault payloads.

## Acceptance boundary

Unit tests cover queue transitions, recovery migration, partial reconciliation,
resume seeks, cleanup ownership, sorting, workspace persistence, and filename
conversion. The real-host GUI gate additionally performs 64 MiB upload and
download pause/resume, encoding restarts, column/sort interaction, permission
error recovery, native drag availability, screenshots, and orderly session
close. Release evidence is recorded in `testing/V2_8_ACCEPTANCE.md`.
