# ADR 0007: Keep SSH credentials ephemeral

- Status: Superseded by ADR 0021
- Date: 2026-07-29

## Context

ztermy needs password authentication and passphrase-protected private keys.
Saved host profiles must remain useful without turning the profile store or
application logs into secret stores.

Qt Quick supplies form values as `QString`, while libssh2 consumes byte
sequences during authentication. Those bytes may otherwise outlive the
connection attempt through copies owned by the application layer.

## Decision

- Saved profiles contain only the authentication method, private-key path, and
  a boolean indicating whether the key requires a passphrase.
- Passwords and passphrases are requested for each connection attempt and are
  never serialized or logged.
- The application converts a submitted credential to a move-only sensitive
  byte container. Its storage is overwritten when authentication finishes and
  again on destruction as a fallback.
- The SSH transport continues to use its own zeroing wrapper for the
  null-terminated passphrase copy required by libssh2.
- Password profiles and passphrase-protected key profiles prompt at connection
  time. Unencrypted key profiles retain one-click connection.

## Consequences

- Restarting ztermy or reconnecting requires the user to enter the credential
  again.
- A future operating-system credential-vault integration requires a separate
  decision and explicit opt-in.
- Qt and operating-system internals can still create transient copies before
  the application receives a form value. ztermy minimizes the copies it owns
  but cannot guarantee whole-process memory locking or eliminate framework
  temporaries.
