# ADR 0036: Orderly application shutdown

Status: accepted

## Context

Tab close already transfers an active SFTP session to a deferred-reap owner so
the GUI remains responsive. Whole-application shutdown is different: the Qt
event loop has stopped, so queued completion callbacks can no longer perform
that reap. Destroying tabs, SFTP sessions, transfers, and log writers one at a
time can serialize cancellation waits, leave a session log unflushed, or let a
late transfer callback mutate recovery state during teardown.

## Decision

- Application shutdown is final and idempotent.
- Request cooperative stop for every transfer worker and active or deferred
  SFTP session before waiting for any of them.
- Stop terminal backends before flushing their session log writers so final
  backend output can be persisted.
- Transfer shutdown joins all workers, ignores queued worker deliveries after
  stop has begun, and persists the last recoverable queue snapshot.
- An incomplete queued, running, cancelling, or attention-required transfer is
  restored on the next launch as `interrupted` and requires an explicit retry.
- Release transfer and SFTP ownership while `AppController` is still alive;
  member destruction is only a fallback, not the primary shutdown mechanism.

## Consequences

- Independent worker cancellations overlap instead of beginning serially as
  each owning object is destroyed.
- Repeated shutdown calls cannot stop a backend twice or restart transfer work.
- Session logs are explicitly flushed and unfinished transfers retain the
  existing safe recovery contract.
- Real-host acceptance still needs to cover closing the application during an
  SFTP listing, upload, download, and active session log.
