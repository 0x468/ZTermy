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

## Detached-window exit boundary (2026-09-27)

Explicit tray exit is application-wide, not a sequence of ordinary Tab closes.
Before the main window closes, the window coordinator captures placement and
enters a final exiting state. It stops placement timers and ignores queued window
synchronization so hidden windows cannot be re-created during shutdown.
Detached windows accept application-exit close events without removing their
Tabs; ordinary user closes still remove only that window's Tabs asynchronously.
Already queued ordinary-close callbacks also respect the final exiting state.

This distinction is required because Qt 6.8.3 cancels its Quit event if any
top-level window rejects close. Previously the detached window always rejected
close before asynchronously deleting its Tabs, leaving the event loop running
after all visible windows disappeared. Cleanup after `application.exec()` was
therefore never reached.

`scripts/verify_window_restore.ps1 -TrayExit` exercises the actual tray-exit
handler and application event loop with live local sessions and a detached
window, from both a visible main window and a main window hidden to the tray.
The external runner checks process termination, no remaining direct child
processes, and preservation of Tab ownership, selection and window placement
across the second startup. Its failure deadline is not a production exit path.

## Consequences

- Independent worker cancellations overlap instead of beginning serially as
  each owning object is destroyed.
- Repeated shutdown calls cannot stop a backend twice or restart transfer work.
- Session logs are explicitly flushed and unfinished transfers retain the
  existing safe recovery contract.
- Real-host acceptance still needs to cover closing the application during an
  SFTP listing, upload, download, and active session log.
