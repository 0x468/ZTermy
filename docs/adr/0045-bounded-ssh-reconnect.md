# ADR 0045: Bound SSH reconnects at the application lifecycle

Status: Accepted

## Context

An SSH transport can disappear after a terminal has connected, but blindly
restarting every failed session would loop on bad credentials, changed host
keys, invalid configuration, or unstable servers. Keeping a password in a tab
to make retries convenient would also cross the credential boundary.

## Decision

- Automatic reconnect is opt-in per saved host and applies only to name
  resolution, refusal, timeout, transport, and remote-close failures.
- Authentication, host-key, channel, cancellation, and protocol failures never
  retry automatically.
- Retry count is bounded to 1–10. Exponential backoff begins at 250–30000 ms
  and is capped at 30 seconds.
- A connection that remains healthy for 30 seconds restores its retry budget;
  short-lived reconnects retain the existing budget so a flapping host cannot
  loop forever.
- The existing `SshTerminalSession` object is restarted after its worker has
  finished. No terminal input or remote process state is replayed.
- Every attempt reconstructs its request from the current saved profile and
  reads credentials from the active vault. Tabs never cache credentials.
- Pending retries are visible and cancellable. Manual reconnect is available
  only for tabs originating from a saved host profile.

## Consequences

Reconnect behavior is deterministic, localized, testable, and safe to cancel.
Password profiles without an available saved credential stop with an explicit
message. A reconnected tab represents a new remote shell even though it keeps
the same local tab identity and session log.

## Verification

Pure domain tests cover failure eligibility and bounded backoff. The authorized
real-host gate starts a key-authenticated shell that exits immediately and
verifies one initial connection plus the configured number of reconnects before
the budget is exhausted.
