# ADR 0062: Bound AI operations reads to immutable typed snapshots

## Status

Accepted

The requirement to expose `session_id` and `session_generation` to the provider
is superseded by ADR 0086. Operations snapshots remain internally attached to
the owning tab and reconnect generation.

## Context

V3.2 lets the terminal assistant inspect SFTP listings, shell history, scripts,
notes, remote telemetry, and port-forwarding status. These services have
different ownership and threading models. Passing live Qt models, SFTP handles,
or mutable job objects into a provider callback would couple provider latency to
the GUI and could silently retarget a tool call after the active tab changes.

Remote listings, history, note names, script metadata, and telemetry are also
untrusted evidence. They may contain prompt-injection text even though the tool
itself is read-only.

## Decision

- Build an immutable `AiOperationsReadSnapshot` on the GUI thread at tool
  dispatch time and attach it to the exact terminal session id and reconnect
  generation.
- Copy only bounded, typed fields. The initial catalog exposes metadata for
  scripts and notes, the current retained SFTP listing, captured shell history,
  the latest telemetry sample, and forwarding job snapshots.
- Page collection tools with an explicit zero-based offset and a limit from 1
  through 100. Provider-visible output remains subject to the turn runner's
  64-KiB result ceiling.
- Require `session_id` and `session_generation` for every operations tool. A
  reconnect or missing session produces an explicit scope error instead of
  consulting whichever tab is active later.
- Mark all returned operations payloads as untrusted evidence. Listing scripts
  and notes does not expose their contents; content reads use separate bounded
  tools.
- Keep remote file I/O out of snapshots. Arbitrary SFTP navigation and file
  reads use a dedicated cancellable asynchronous request path in a later 0.3.2
  slice.

## Consequences

- Provider callbacks never hold live Qt models or forwarding jobs and never
  block on remote I/O.
- The snapshot has a deliberate copy cost. It is capped (including 500 retained
  SFTP rows, 500 recent history entries, and bounded text fields) and is created only when a
  tool call is dispatched.
- Current-directory SFTP reads are immediately useful, while arbitrary remote
  reads remain visibly unavailable until the async SFTP contract is complete.
- New operations data must define a typed bound and scope rule before being
  added to the catalog.
