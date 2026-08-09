# ADR 0047: explicit SSH proxy tunnels

## Status

Accepted for V2.9.

## Context

ztermy needs enterprise HTTP proxy support and general-purpose SOCKS proxy
support without delegating the SSH session to a subprocess. Proxy credentials
must obey the same persistence and zero-plaintext rules as host credentials,
and the proxy handshake must remain cancellable on the SSH worker thread.

## Decision

- Support SOCKS5 CONNECT and HTTP/1.1 CONNECT as the explicit proxy modes.
- Negotiate the proxy protocol directly over `SshByteTransport`, then reuse the
  established byte stream for libssh2. No proxy-specific behavior enters the
  terminal, SFTP, transfer, or telemetry layers.
- SOCKS5 uses remote name resolution through the domain-name address form.
  Optional RFC 1929 username/password authentication is bounded to 255 bytes.
- HTTP CONNECT accepts bounded HTTP/1.0 and HTTP/1.1 responses, limits headers
  to 16 KiB, and reads exactly through the header terminator so an eager proxy
  cannot cause the SSH banner to be consumed. Optional Basic credentials are
  emitted only in the proxy handshake buffer, which is overwritten after use.
- Proxy passwords use a distinct `ProxyPassword` vault key. Profile JSON stores
  only a credential reference and never the secret.
- Every operation has one deadline and stop token. Partial reads/writes and
  `WouldBlock` are normal protocol behavior; cancellation and transport failure
  remain distinguishable.

## Consequences

The direct SSH path remains unchanged and proxy protocol logic is covered by a
deterministic scripted transport. HTTP proxy authentication is currently Basic
over the user-selected proxy connection; TLS-to-proxy, NTLM, Negotiate, and PAC
resolution are intentionally outside V2.9. ProxyJump is composed separately on
the same byte-transport boundary and still verifies every SSH hop.
