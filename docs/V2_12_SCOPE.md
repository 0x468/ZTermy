# V2.12 scope: recursive and batch SFTP

Status: implemented in `0.2.12`; manual acceptance remains tracked separately

V2.12 extends the proven V2.8 regular-file transfer path into a bounded batch
job system. A batch is a durable parent operation containing directory actions
and regular-file child transfers; the existing `TransferExecutor` remains the
only byte-copy fast path for an individual file.

## Included

- multi-selection in list and tree views, with keyboard range/toggle/select-all
  behavior and accessible selected-count feedback;
- recursive upload and download of files and directories, including empty
  directories, aggregate discovery/progress/speed/ETA, and per-child details;
- explicit conflict policies (`ask`, `replace`, `rename`, `skip`); safe,
  idempotent `replace` and `skip` decisions may apply to the remaining queued
  children without changing an already-running child, while `rename` always
  remains explicit because each destination must be unique;
- bounded pause, resume, cancel, retry, dismiss, and interrupted-start recovery
  at both batch and child level;
- deterministic directory creation ordering, bounded regular-file concurrency,
  partial-file cleanup, local destination reveal, and source/destination copy;
- drag-and-drop upload of multiple local files/directories and the existing
  completed-download drag-out path for regular files;
- light/dark, English/Chinese, keyboard, screen-reader, compact/regular, empty,
  discovery, conflict, progress, partial failure, recovery, and completion UI.

## Traversal and safety contract

- Local and remote trees are enumerated off the GUI and Qt Quick render threads.
- Traversal is iterative, cancellation-aware, and bounded to 64 levels, 100,000
  discovered entries, 100,000 leaf jobs, and normalized paths of at most 4,096
  UTF-8 bytes. Exceeding a limit produces an explicit partial-plan failure.
- Symbolic links are never followed recursively in V2.12. Remote links and local
  reparse/symlink entries are reported as skipped items; this prevents loops and
  escape outside the selected root.
- `.` and `..`, NUL, invalid encodings, duplicate normalized destinations, path
  traversal, reserved Windows names, and source/destination type mismatches are
  rejected before a child reaches the transfer queue.
- Directory creation is parent-before-child. File transfers may run with the
  existing bounded concurrency. Completion is not reported until all children
  and final directory metadata actions have reached terminal states.
- Download writes retain the `.ztermy-part` byte-range resume contract; upload
  retains the remote partial path and atomic rename contract where supported.

## Persistence and recovery

- Batch metadata, selected roots, relative plan entries, conflict policy,
  discovery state, aggregate counters, and child links use a versioned schema.
- Credentials, secrets, live SFTP handles, directory listings, local file
  contents, and remote payloads are never persisted.
- On restart, completed children remain completed, partial regular files are
  validated by the existing byte-range contract, running/discovering children
  become interrupted, and the user explicitly resumes or cancels the batch.
- Corrupt, unsupported, over-limit, cyclic, duplicate, or escaping plans are
  rejected as a unit without deleting source or destination data.

## Resource and performance boundaries

- At most 32 retained batches, 100,000 children per batch, two running regular
  file workers by default, and one owned discovery worker per active batch.
- UI snapshots are throttled/coalesced; the controller does not rebuild a
  100,000-row QML object tree. The transfer center virtualizes children and
  publishes aggregate deltas at a bounded cadence.
- Cancellation is checked between directory entries and byte chunks. Every
  planner, transfer, cleanup, and drag-out worker is owned and joined at shutdown.

## Deferred

- following symbolic links, preserving ACLs/owners/xattrs/alternate streams,
  sparse-file topology, hard-link reconstruction, remote-to-remote copy, rsync
  delta transfer, synchronization/mirroring, background service transfers, and
  remote editing;
- shell-based archive shortcuts (`tar`/`zip`) and implicit command execution;
- direct native virtual drag-out of remote files or directories before a local
  download exists, pending a separate Windows descriptor/cancellation review.

## Exit gate

V2.12 completes only after planner/domain/store/manager/controller/UI tests,
large-tree and cancellation stress, real-host recursive upload/download,
interrupted recovery, symlink-boundary evidence, the serial real-window/DPI
matrix, static analysis, static Release, and packaged artifacts pass. Subjective
Explorer drag behavior and physical multi-monitor checks remain recorded for the
V2.14 owner pass.
