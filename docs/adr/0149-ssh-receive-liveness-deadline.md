# ADR 0149: Bound unresponsive SSH sessions by inbound activity

- Status: Accepted
- Date: 2026-10-25

## Decision

New SSH sessions default to a 30-second SSH keepalive interval and a three-interval
receive deadline. Explicit saved zero intervals remain disabled; saved nonzero
intervals and failure limits remain unchanged. No persisted field or schema is added.

libssh2 keepalive send success only means local transport progress, not a reply.
Use a steady-clock timestamp for positive reads from the session's byte transport,
including keepalive replies and auxiliary-channel data. Outbound writes, socket
readiness, zero-byte reads and EAGAIN do not refresh it. Terminal silence alone
never implies failure. libssh2 still handles packet authentication and protocol
errors; this timestamp is a connection-liveness heuristic, not authentication of
an individual keepalive acknowledgement or protection against malicious peers.

Start probing once the terminal is connected. An outstanding probe expires after
interval times failure limit with no incoming data, using libssh2's minimum
effective interval of two seconds. Incoming data clears the outstanding probe;
send success does not. Never expire an idle connection before a probe has had
its response window, including when the failure limit is one. Retain send-error
handling. Expiry reports a timeout through the existing
disconnect/reconnect and retained-output UI, without replaying terminal input.

## Verification

A loopback SSH fixture silently discards encrypted server output while keeping
TCP open and accepting client writes. Test timeout despite a successful local
write, an idle shell with protocol replies, explicit disablement, and the one-second
configuration clamp. No developer shell, remote host, or firewall rules are used.
