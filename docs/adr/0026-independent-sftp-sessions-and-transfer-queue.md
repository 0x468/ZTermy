# ADR 0026: Use independent SFTP sessions and a bounded transfer queue

Status: accepted, 2026-08-02

## Context

V1.6 adds SFTP browsing and file transfer to active SSH terminals. The current
interactive SSH architecture owns a non-blocking libssh2 session, PTY channel,
auxiliary command channel, socket, and command queue on one worker thread.
libssh2 session state is mutable and must not be advanced concurrently.

The main ownership choices are:

1. Add SFTP operations to the interactive terminal worker and serialize every
   directory read and file payload with PTY I/O.
2. Share the authenticated libssh2 session between a terminal worker and SFTP
   workers behind locks.
3. Establish independent authenticated SSH/SFTP sessions for browsing and
   background transfer work while reusing ztermy's endpoint, trust, and
   credential policies.

The first choice risks terminal latency and head-of-line blocking. The second
has fragile cancellation and lock behavior, couples unrelated state machines,
and still lets a large payload monopolize one transport window. It would also
make future backend replacement harder.

## Decision

Use independent SSH/SFTP sessions:

- A terminal-attached SFTP browser owns one socket, libssh2 session, SFTP
  handle, worker, and request queue. Closing the panel cancels outstanding
  work and destroys that ownership chain without touching the terminal PTY.
- Background transfers are scheduled by one application-level manager with a
  small fixed concurrency limit. A transfer connection never owns or calls
  the browsing or terminal session. Initial V1.6 workers may close their
  connection after a task; connection pooling is a later optimization behind
  the same boundary.
- Shared code performs socket connection, key exchange, exact host/port trust
  verification, and password/private-key authentication. The SFTP layer does
  not weaken unknown/changed-host behavior and does not invent a second
  known-host store.
- `LIBSSH2_ERROR_EAGAIN` remains a scheduling state. Every SFTP operation is
  advanced on its owner worker with deadline and stop-token checks.
- Domain-facing results contain normalized remote entries, transfer progress,
  structured failures, and opaque request/task identifiers. No libssh2 handle,
  password, passphrase, key content, or QML type crosses the boundary.
- The transfer queue is runtime state, not credential or profile persistence.
  A task that needs a locked/unavailable credential becomes NeedsAttention and
  can be retried after user interaction.

## File-safety rules

- Downloads write to a uniquely owned sibling temporary file and atomically
  publish the requested destination only after success.
- Existing destinations are never truncated before an explicit conflict
  decision. Cancellation/failure removes only temporary artifacts created by
  that task.
- Upload replace behavior is explicit. V1.6 does not claim atomic remote
  replacement where the server lacks suitable rename semantics.
- Symlinks are listed as symlinks and never recursively traversed by V1.6.
- Task/log/status data redact local and remote command-like secret material;
  terminal input and credentials are never logged.

## Consequences

Opening SFTP performs an additional SSH authentication, and concurrent
transfers can create additional bounded connections. That is an intentional
latency/resource tradeoff for terminal responsiveness, understandable
cancellation, and race-free ownership. Host-key and credential flows must be
reusable services rather than terminal-only implementation details.

The boundary supports a later dual-pane manager, connection pooling,
resumable transfers, recursive directory jobs, proxy/jump hosts, or another
SSH backend without changing QML task semantics.

## Validation gates

- Directory listing, navigation, stale-result suppression, and cancellation
  are deterministic under injected EAGAIN, timeout, disconnect, and panel
  close.
- A large background transfer does not measurably block terminal input or
  browsing refresh and never calls a foreign session from another thread.
- Unknown and changed host keys behave identically to terminal SSH; rejected
  authentication cannot create an SFTP handle.
- Conflict, partial-download, replace, retry, and cancellation fault tests do
  not corrupt or delete pre-existing files.
- Repeated panel open/close and transfer cancellation leave no socket, SFTP
  handle, worker, temporary file, or secret-bearing queue record behind.

