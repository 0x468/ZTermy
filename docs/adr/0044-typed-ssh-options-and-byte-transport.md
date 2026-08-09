# ADR 0044: typed SSH options and a cancellable byte-transport boundary

## Status

Accepted for V2.9

## Context

The V2.8 connection bootstrap owns a `WindowsTcpSocket` directly and passes it
through every libssh2 operation. This is simple and correct for direct SSH, but
it cannot represent a final SSH session carried through a ProxyJump channel.
Adding proxy fields only in QML or profile JSON would create settings that the
transport cannot honor.

The same milestone adds keepalive, reconnect, environment requests, startup
commands, terminal type, and agent authentication. These settings affect
different phases and must not become an unvalidated map shared across UI,
persistence, and worker threads.

## Decision

- Define typed, domain-owned SSH connection options with bounded values and
  conservative defaults. Persist them through a versioned profile schema and
  copy them into an ephemeral connection request.
- Introduce a small cancellable byte-transport interface used by libssh2 I/O.
  Direct TCP is the first implementation. Proxy and ProxyJump transports are
  composed behind the same interface after direct behavior is regression-tested.
- A hop chain owns every preceding authenticated session and channel for the
  lifetime of the final session. Destruction closes from the final hop outward;
  no forwarding worker is detached.
- Each endpoint has an independent host-key verification and credential request.
  Secrets are scoped to the hop that consumes them and cleared immediately after
  authentication.
- Reconnect constructs a fresh transport/session chain. It restores only
  declarative intent—profile, geometry, encoding, and permitted startup policy—
  and never replays terminal input or pretends to resume a remote process.
- Windows OpenSSH agent authentication and remote agent forwarding are separate
  features. Supporting the first does not silently enable the second.

## Consequences

- Existing direct SSH code must be migrated behind an interface before
  ProxyJump can ship, increasing V2.9 implementation cost.
- Terminal, SFTP, transfer, telemetry, and V2.10 forwarding can share connection
  policy without sharing GUI objects.
- Test doubles can deterministically inject partial reads, timeouts, disconnects,
  and cancellation at each hop.
- The typed model prevents unsupported fields from becoming inert UI and keeps
  secrets outside persistent option structures.

## Implementation note

libssh2 is configured with its non-blocking send/receive callbacks for every
session, including direct TCP. The callbacks address a heap-owned
`SshByteTransport`; that stable object outlives the libssh2 session and converts
would-block, close, timeout, cancellation, and system failures at one boundary.
`WindowsTcpSocket` is the direct implementation. This means direct SSH exercises
the same callback path that a later ProxyJump channel will use instead of
retaining a second socket-only fast path.
