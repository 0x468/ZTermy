# ADR 0049: owned native port-forwarding jobs

Status: accepted for V2.10

## Context

Local, remote, and dynamic forwarding must serve multiple byte streams while
remaining independent from interactive terminal tabs. libssh2 sessions are not
a safe cross-thread shared object, and attaching tunnels to the large terminal
worker would couple unrelated latency, reconnection, and shutdown behavior.
Rules also need auto-start and may outlive every visible terminal.

## Decision

- Persist forwarding rules as a separate non-secret schema keyed by stable rule
  ID and saved SSH profile ID. Credentials remain in the configured vault.
- Give each active rule one independent authenticated SSH connection, one
  `std::jthread`, and one event loop that exclusively owns its libssh2 session,
  listener, forwarded channels, accepted sockets, buffers, and retry state.
- Reuse `SshConnectionRequest` construction and
  `establishAuthenticatedSshConnection` so explicit proxy, ProxyJump, host-key,
  credential, Agent, cancellation, and secret-clearing behavior remain one
  security boundary.
- Multiplex a bounded client set inside the rule worker. Do not spawn detached
  threads per client and do not expose payload bytes to Qt signals or logs.
- Keep runtime snapshots small and value-based: rule state, public bind
  endpoint, client count, byte counters, retry time, and sanitized error. The
  QML layer only renders snapshots and sends commands.
- Default local and dynamic listeners to loopback. Treat broader binds as an
  explicit exposure choice rather than silently editing Windows Firewall.

## Consequences

- A terminal can close without stopping an explicitly started tunnel, and a
  busy tunnel cannot starve terminal rendering or input.
- Each rule pays for an SSH connection. The accepted limit of 16 active rules
  trades connection cost for simple ownership, deterministic recovery, and
  absence of cross-thread libssh2 locking.
- Auto-start is deterministic but may wait for vault unlock or host-key input.
  It cannot become a hidden background credential prompt.
- A later optimization may group compatible rules only behind a new ADR that
  preserves exclusive session-thread ownership and proves fair multiplexing.
