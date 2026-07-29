# ADR 0012: Interruptible SSH I/O and content-free latency metrics

Status: accepted

## Context

The SSH worker previously polled the remote channel for up to 25 milliseconds
before returning to its application-command queue. Input, resize, selection,
and search commands submitted during that wait could therefore inherit an
application-side delay that was visible even on a low-latency network.

V1 also needs repeatable evidence for local and SSH input responsiveness.
Recording individual samples or input payloads would either grow without a
bound or create an unnecessary terminal-content disclosure risk.

## Decision

The Windows socket wait accepts an optional manual-reset interrupt event.
While a libssh2 channel read is waiting, WinSock network events, stop
requests, and application commands are waited on together. Enqueuing any SSH
session command signals the event. The worker swaps the queue and resets the
event while holding the same queue mutex, preventing a producer wake-up from
being lost between those operations.

Local and SSH sessions measure input queue latency from GUI-thread enqueue to
worker-thread dequeue. This is application scheduling latency, not remote echo
latency, network round-trip time, shell processing time, or rendering latency.

Measurements use a fixed-size atomic histogram. Logs contain only sample
count, bounded P50/P95/P99 values, and the actual maximum in microseconds.
They never contain terminal input, clipboard content, credentials, private-key
material, or secret-bearing command lines. The histogram has constant memory
usage and is reset for each session.

The V1 interactive target is an input queue P95 no greater than 16
milliseconds after at least 100 input samples in a representative local or
LAN SSH session. Automated coverage verifies histogram behavior and that a
pending socket read is interrupted promptly. Interactive runtime evidence is
still required for the complete Qt input-to-session path.

## Consequences

- SSH commands no longer wait for the 25-millisecond channel-read poll period.
- Stop requests can wake a pending interruptible read without waiting for the
  remote peer.
- The wait path temporarily associates a WinSock event with the socket and
  leaves the already non-blocking socket in non-blocking mode after removal.
- Metrics are safe for routine Debug logs but cannot diagnose network or
  remote-shell latency.
- Real local and SSH sessions remain part of Windows 11 acceptance because
  unit tests cannot establish interactive latency or perceived responsiveness.
