# ADR 0005: Serialize each SSH session on one worker thread

Status: accepted

## Context

libssh2 exposes non-blocking operations, but a single session and its channels
must not be driven concurrently without external synchronization. SSH network
I/O must also never block the Qt GUI or Qt Quick render thread.

Host-key confirmation introduces another asynchronous boundary: the worker must
pause before authentication until the UI explicitly accepts or rejects the
observed fingerprint. A stale or premature confirmation must not trust a host.

## Decision

Each `SshTerminalSession` owns one `std::jthread`. That worker exclusively owns
and drives its TCP socket, libssh2 session, terminal channel, and terminal
parser. It serializes queued input and resize commands with bounded 25 ms reads,
so no two threads enter one libssh2 session.

Connection setup is cancellable through the worker stop token. Unknown host keys
move the state machine to `AwaitingHostKeyConfirmation`; confirmation methods
are effective only while that state is actively waiting. Changed keys are never
offered an ordinary accept path. An accepted key may be used once or atomically
persisted through the application-owned known-host store before authentication.

Terminal snapshots are immutable and coalesced before queued delivery to the Qt
thread. Passwords, passphrases, terminal input, private-key contents, and
secret-bearing command lines are not logged.

## Consequences

- Network stalls cannot block the GUI thread.
- libssh2 session access has a simple single-thread ownership rule.
- Input latency is bounded by the short read interval until a native event-loop
  integration is justified by profiling.
- One SSH connection currently owns one interactive terminal channel.
- Password and encrypted-key prompts remain a later UI integration step; their
  secrets must be passed directly to the worker and securely erased.
