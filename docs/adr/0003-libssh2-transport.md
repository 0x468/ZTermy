# ADR 0003: Use libssh2 behind a ztermy-owned SSH transport

Status: accepted, amended 2026-07-29

## Context

V1 requires interactive SSH sessions with password and key authentication,
strict known-host verification, non-blocking terminal I/O, and a path to SFTP.
The SSH implementation must build with MSVC and Ninja, work in both the dynamic
developer build and the static release, and never expose credentials to QML or
logs.

Qt does not provide an SSH client. Implementing SSH or invoking an interactive
OpenSSH subprocess would either create an unacceptable security maintenance
burden or make host-key confirmation, authentication prompts, cancellation,
and error classification depend on parsing command-line UI.

The main library candidates are:

- `libssh2`: client-only C API, permissive license, caller-owned sockets,
  non-blocking operation, known-host APIs, authentication, channels, and SFTP.
- `libssh`: higher-level client/server API with SFTP, but its LGPL license adds
  distribution obligations that are especially awkward for a static build.
- wolfSSH: capable client/server implementation, but its GPLv3 or commercial
  licensing is not a suitable default while ztermy has no selected license.

References:

- <https://libssh2.org/>
- <https://libssh2.org/docs.html>
- <https://libssh2.org/license.html>
- <https://api.libssh.org/stable/index.html>
- <https://www.wolfssl.com/documentation/manuals/wolfssh/chapter10.html>

## Decision

Use the pinned libssh2 1.11.1 release behind a ztermy-owned C++ transport
interface. No libssh2 type crosses the infrastructure boundary.

The first Windows integration used libssh2's WinCNG backend to avoid adding a
second cryptographic runtime. Real-host validation proved that libssh2 1.11.1
builds WinCNG with `LIBSSH2_ED25519=0`: the same `id_ed25519` key succeeded
through Windows OpenSSH but could not be loaded by libssh2.

The crypto backend is therefore OpenSSL 3. Dynamic developer builds use the
OpenSSL runtime matching the selected MSVC runtime (`MDd` for Debug and `MD`
for Release-like builds); packaging must deploy its Crypto DLL. The static
release resolves the `MT` static Crypto archive. libssh2 itself remains
statically linked and debug tracing remains disabled. This keeps modern
Ed25519 file authentication in-process without requiring users to run an SSH
agent or converting their keys.

The transport runs on a worker thread and uses non-blocking libssh2 calls over
a ztermy-owned socket. `LIBSSH2_ERROR_EAGAIN` is a scheduling state, not a
failure. Cancellation and timeouts are controlled outside libssh2 so no SSH
operation blocks the GUI or render thread.

Host authentication is a mandatory state between key exchange and user
authentication:

1. Read the negotiated server host key.
2. Check the exact host and port against ztermy's known-host store.
3. Continue automatically only for an exact match.
4. Pause and request explicit confirmation for an unknown key.
5. Block a changed or malformed key; never replace it automatically.

Unknown-host confirmation displays the algorithm and SHA-256 fingerprint.
Passwords, passphrases, private-key contents, keyboard-interactive responses,
and terminal input are never logged.

## Validation gates

- MSVC dynamic Debug and static Release builds use the same pinned revision.
- The selected libssh2 crypto engine reports OpenSSL, not WinCNG.
- Password authentication succeeds against a real OpenSSH server.
- At least one encrypted private-key flow and Windows OpenSSH agent flow pass.
- Required modern host-key and user-key algorithms are enumerated and tested.
- Unknown keys pause before authentication; changed keys always block.
- Refusal, timeout, authentication failure, host-key failure, and remote close
  produce distinct domain errors.
- Twenty connect/disconnect cycles leave no socket, channel, session, or worker
  behind.
- Sustained SSH output remains responsive and adds no application-side input
  batching delay.

## Consequences

ztermy owns connection state, trust policy, credential lifetime, scheduling,
and error mapping. libssh2 owns SSH protocol encoding, cryptographic
negotiation, channels, authentication mechanisms, and SFTP protocol details.
The boundary allows a backend replacement without changing terminal state,
QML, host persistence, or credential UI.
