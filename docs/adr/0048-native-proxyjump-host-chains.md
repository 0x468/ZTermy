# ADR 0048: native ProxyJump host chains

## Status

Accepted for V2.9.

## Context

Saved SSH targets need to traverse one or more bastion hosts without handing
the session, terminal, SFTP, transfers, or credentials to an `ssh.exe`
subprocess. Each hop may have its own authentication method and explicit proxy,
and each SSH server identity must be presented to the user without ambiguity.

## Decision

- A target profile stores an ordered list of saved profile IDs. The list is
  limited to three distinct entries, cannot contain the target itself, and is
  validated against the complete profile document before persistence.
- A referenced profile contributes endpoint, authentication, private-key, and
  explicit-proxy settings. Its own jump list is not expanded; the target owns
  the complete, visible route order and therefore cannot create recursive or
  hidden chains.
- Each authenticated hop opens a libssh2 `direct-tcpip` channel to the next
  endpoint. That channel implements `SshByteTransport`, so the next libssh2
  session can be layered recursively without special cases in terminal, SFTP,
  transfer, or telemetry code.
- Every hop performs an independent SSH handshake, known-host lookup, host-key
  decision, and authentication. Confirmation signals include the exact display
  endpoint so a jump host can never be mistaken for the final target.
- Jump credentials remain in the configured credential vault under their own
  profiles. A chain cannot start until every password, required key passphrase,
  and authenticated proxy password is available. Secrets are cleared from the
  complete connection request on every success and failure path.
- A profile referenced by another profile cannot be deleted until the route is
  edited. Profile schema v6 migrates older documents with an empty route.

## Consequences

Multi-hop connections remain native, cancellable, and usable by all SSH-backed
features. Nested sessions increase handshake latency and resource use in a
bounded, visible way. A saved profile can be reused by several target routes,
so editing its endpoint or credential intentionally affects those routes. The
three-hop limit can be revisited only with new latency, teardown, and UI
evidence.
