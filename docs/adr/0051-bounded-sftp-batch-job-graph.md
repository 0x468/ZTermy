# ADR 0051: bounded SFTP batch job graph

Status: accepted for V2.12

## Context

The existing transfer queue intentionally models one regular file per task. It
already provides resumable byte ranges, conflict review, bounded concurrency,
credential/host-key routing, cancellation, recovery, and deterministic worker
ownership. Recursive transfer adds tree discovery, directory dependencies,
aggregate progress, partial failure, and policy scope. Turning one directory
into a single opaque transfer task would discard the proven file contract and
make resume, diagnostics, and cancellation coarse. Flattening an unbounded tree
directly into the existing queue would make memory, UI, and persistence costs
uncontrolled.

## Decision

Introduce a durable `TransferBatch` parent and a bounded dependency plan:

- a planning worker enumerates one or more selected roots iteratively and emits
  normalized relative entries;
- directory actions and regular-file leaf tasks have stable identifiers and
  explicit parent/dependency links;
- the existing `TransferExecutor` continues to copy every regular file;
- one batch owns policy, aggregate counters, discovery/error state, and child
  lifecycle, while the regular-file queue retains its concurrency limit;
- planners and workers communicate by immutable snapshots/deltas and never
  expose live SFTP handles to QML;
- recursive symbolic-link following is prohibited, and hard limits are checked
  before appending every discovered entry;
- recovery persists declarative plan and counters, not credentials or handles.

The first schema is intentionally general enough for uploads and downloads but
does not model remote-to-remote edges or synchronization semantics.

## Consequences

- Single-file transfers keep their tested performance and recovery behavior.
- A directory with partial failures can be inspected and retried at child or
  parent scope without falsely restarting completed files.
- Discovery has explicit progress even when total bytes are not yet known.
- Batch snapshots and child virtualization add implementation complexity, but
  avoid a QML object per discovered entry and keep memory/time bounded.
- Empty directories and directory ordering become first-class plan entries.
- Symlinks are visible as skipped results rather than silently followed or
  copied with ambiguous platform semantics.

## Rejected alternatives

- **One opaque recursive worker:** weak resume, conflict, progress, retry, and
  diagnostics; duplicates the regular-file transfer implementation.
- **Unbounded flattening into `TransferQueue`:** permits hostile or accidental
  trees to exhaust memory and floods UI/persistence updates.
- **Shelling out to `scp`, `sftp`, `tar`, or PowerShell:** violates the native
  transport boundary, complicates host-key/credential handling, and makes
  cancellation and diagnostics process-dependent.
- **Follow links with visited-path tracking:** remote identity and mount/bind
  semantics are not reliable enough for a safe V2.12 default.

