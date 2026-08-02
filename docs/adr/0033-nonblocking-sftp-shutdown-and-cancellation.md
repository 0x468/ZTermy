# ADR 0033: Non-blocking SFTP shutdown and observable cancellation

Status: accepted

## Context

An SFTP browser owns a worker thread and long-running libssh2 operations. A tab
can be closed while a directory request or transfer is active. Joining that
worker synchronously from the Qt Quick GUI thread can freeze the window, while
destroying session state before the worker exits risks use-after-free and heap
corruption. A running transfer also cannot honestly become `Cancelled` at the
instant the user requests cancellation because its worker may still be
unwinding and releasing remote and local resources.

The initial browser also opened `/`, which is not the user's normal starting
location and may expose many inaccessible directories.

## Decision

- Closing a tab requests cooperative SFTP cancellation and moves the session to
  an application-owned deferred-reap collection. The GUI thread never joins the
  worker; the session is destroyed only after the worker reports completion.
- Transfer cancellation has an explicit `Cancelling` state. It continues to
  occupy a concurrency slot until the worker result finalizes it as
  `Cancelled`, `Completed`, or `Failed`.
- The SFTP client canonicalizes `.` through the negotiated server session. The
  resulting absolute path is the browser home and initial directory.
- A failed navigation preserves the last successful directory listing and is
  presented as an inline error. The full unavailable state is reserved for a
  session that has never produced a usable listing.
- Finished transfer records may be dismissed individually or cleared as a
  group; active and attention-required work cannot be discarded.

## Consequences

- Closing an SFTP tab remains responsive even while remote I/O is unwinding,
  and ownership remains valid until the worker has stopped.
- UI progress and concurrency accounting match the actual lifetime of a
  cancellation request.
- SFTP starts where users expect and retains recoverable context after a
  permission or path error.
- Shutdown and cancellation behavior require focused lifecycle tests plus a
  real-host close/cancel acceptance pass.
