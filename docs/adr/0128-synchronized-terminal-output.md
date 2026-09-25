# 0128: Bound synchronized output at the session presentation boundary

Status: Accepted

## Context

Ghostty parses DEC private mode 2026, but ztermy builds and publishes its own
immutable terminal snapshots. Parser support alone does not suppress intermediate
TUI frames. Output parsing must remain live so terminal queries do not deadlock a
program waiting for a response inside a synchronized update.

## Decision

- Local and SSH sessions defer output-driven snapshots while mode 2026 is set.
- Continue parsing, returning protocol replies and servicing input immediately.
- An owner-thread precise timer wakes the session worker after one second.
  The worker cancels an abandoned synchronization mode and builds the pending
  frame. A coarse timer can fire too early for a strict deadline check and must
  not be used for this one-shot fallback.
- Host interactions that request a new view, including selection, scrolling,
  search, resizing and theme changes, cancel the hold. User interaction takes
  precedence over an application's presentation hint.
- Timeout and host interruption reset the engine mode as well as the session
  deadline, allowing a later application update to synchronize again.
- On EOF/disconnect, flush pending content before notifying the UI that the
  session has stopped. SSH transfers the final snapshot into the owner-thread
  completion event because normal delivery ignores disconnected sessions.

## Evidence and limitations

Raw local-session boundary tests exercise the real output consumer, command
worker and Qt timers without ConPTY rewriting the supplied VT stream. They
demonstrated and then verified fixes for loss of synchronization after timeout
and delayed selection. A separate SSH completion test demonstrated final-frame
loss before the disconnect notification and passed after the fix. An engine
test verifies CPR still responds while mode 2026 is active.

A loopback SSH fixture additionally exercises the actual libssh2 transport,
authentication, worker and snapshot path: CPR during synchronization, held
intermediate frames, timeout recovery, a new synchronization interval, resize
interruption and final output on disconnect all pass. The fixture never starts
a shell or exposes forwarding or filesystem operations. Paramiko is a test-only
dependency in the explicitly selected Python environment, not an application
dependency.

This does not establish behavior on every Windows ConPTY version. The installed
ConPTY changes the fixture's synchronization interval; that separate platform
integration check explicitly reports the missing precondition. See
`docs/research/TERMINAL_ENGINE.md` for the audit and runtime evidence.
