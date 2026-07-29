# SSH transport testing

Status: non-blocking connection, handshake, and strict host-key classification
automated; pre-authentication real-host validation available

## Automated coverage

The `windows-tcp-socket` test uses loopback only. It verifies:

- successful connection to a local listener;
- bounded failure for a closed local port;
- pre-requested cancellation;
- invalid endpoint rejection; and
- move-only socket ownership and deterministic close.

The `ssh-handshake` test uses a local peer that accepts TCP but intentionally
does not speak SSH. It verifies:

- libssh2 session initialization in non-blocking mode;
- handshake timeout without busy waiting;
- cancellation before and during a blocked handshake;
- invalid socket rejection; and
- session cleanup after incomplete handshakes;
- invalid password credential rejection; and
- rejection of authentication before handshake and host-key trust.

The `ssh-host-key` test verifies:

- exact host, port, algorithm, and key matching;
- unknown hosts and ports;
- changed keys and algorithms;
- malformed records; and
- OpenSSH-style SHA-256 fingerprint formatting.

The `known-hosts-store` test verifies:

- a missing store is treated as empty;
- accepted public host keys survive an atomic save/load round trip;
- parent directories are created;
- a changed key remains blocked after reload;
- malformed JSON and unsupported schema versions are rejected;
- duplicate endpoint-and-algorithm records are rejected; and
- fractional ports and invalid base64 keys are rejected.

Run the focused tests through the configured Debug preset:

```powershell
ctest --test-dir build/msvc-dynamic-debug -R "windows-tcp-socket|ssh-handshake|ssh-host-key|known-hosts-store" --output-on-failure
```

The full Debug and static Release test suites must also pass before changes are
committed.

## Real-host pre-authentication gate

`ssh-real-host` is skipped unless `ZTERMY_TEST_SSH_HOST` is set. When enabled,
it connects and performs the SSH handshake, extracts the public host key, and
prints only its algorithm and SHA-256 fingerprint. It does not send a username,
password, passphrase, or keyboard-interactive response. It also verifies that
authentication is blocked while the host is unknown, then opens the
authentication gate only after an exact in-memory trust match. A zero timeout
prevents that gate test from sending its synthetic credentials.

```powershell
$env:ZTERMY_TEST_SSH_HOST = "server.example.test"
$env:ZTERMY_TEST_SSH_PORT = "22"
build/msvc-dynamic-debug/ztermy_ssh_real_host_tests.exe
```

Before authentication is implemented or tested, compare the printed fingerprint
with a trusted value obtained directly from the server administrator or server
console. For an ECDSA P-256 host key, a typical server-side command is:

```sh
ssh-keygen -lf /etc/ssh/ssh_host_ecdsa_key.pub -E sha256
```

Expected result:

- the test reports the host as unknown before any trust record exists;
- the algorithm and SHA-256 fingerprint match the server-side value; and
- no credential or terminal input appears in application or test logs.

Later gates will add explicit coverage for accepting an unknown key, matching a
stored key, blocking a changed key, authentication failure, remote close, and
repeated connect/disconnect cycles. Passwords, passphrases, private key
contents, keyboard-interactive responses, and terminal input must never appear
in the test output or application log.

## Password authentication gate

The transport exposes bounded, cancellable password authentication only after:

1. the TCP socket is connected;
2. the SSH handshake is complete; and
3. the observed host key exactly matches a trusted host-and-port record.

Passwords are non-owning call-time views. Callers must keep the backing memory
alive for the call, must not log it, and should overwrite and release their
credential buffer as soon as the authentication attempt finishes. Real password
authentication remains a human runtime gate and is not enabled through CTest
environment variables or command-line arguments.

## Private-key authentication gate

Private-key authentication has the same handshake and exact-host-trust
preconditions. The transport receives a UTF-8 path and lets libssh2 read and
sign with the key; ztermy does not log the path or key contents. A call-time
passphrase is copied only to ensure null termination, securely overwritten
before release, and never logged.

The real-host test remains skipped unless all four non-secret gate variables
are set:

```powershell
$env:ZTERMY_TEST_SSH_HOST = "server.example.test"
$env:ZTERMY_TEST_SSH_USERNAME = "test-user"
$env:ZTERMY_TEST_SSH_PRIVATE_KEY = "$HOME/.ssh/id_ed25519"
$env:ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT = "SHA256:verified-value"
build/msvc-dynamic-debug/ztermy_ssh_real_host_tests.exe
```

This automated gate intentionally supports only an unencrypted test key.
Passphrase-protected keys must be exercised through the interactive credential
UI so the passphrase never appears in an environment variable, command line,
test report, or log. The test refuses authentication if the observed host
fingerprint differs from the independently supplied expected fingerprint.

## Terminal channel gate

After exact host-key verification and authentication, the transport can open
one `xterm-256color` session channel, request a PTY, start the remote shell,
resize it, transfer byte streams, and close it deterministically. All channel
operations use the same bounded and cancellable non-blocking socket wait as the
handshake and authentication stages.

The gated real-host suite opens an 80 by 24 PTY, resizes it to 100 by 30, and
closes the shell without sending or logging terminal input. It uses the same
four non-secret variables as the private-key authentication gate.

## Application session gate

`ztermy_ssh_terminal_session_tests` verifies invalid profile rejection without
network access. With the four private-key gate variables set, it also exercises
the complete worker-thread flow against the real server:

1. connect and negotiate without blocking the Qt test thread;
2. stop at the unknown-host boundary and expose the observed fingerprint;
3. compare that fingerprint with the independently supplied expected value;
4. verify that a premature confirmation had no effect, then explicitly accept
   and remember the observed key;
5. authenticate, open the remote shell, and publish a terminal snapshot;
6. queue a resize and stop the worker cleanly;
7. reconnect from the persisted trust record without another confirmation.

The test never sends terminal input and uses a temporary known-host store.

## Failure recovery UI

Exercise name-resolution failure, connection refusal, timeout,
authentication rejection, and remote close through non-sensitive test
endpoints.

Expected:

- Each failure produces its distinct non-secret status message.
- A failed SSH tab shows a visible recovery panel with the same status instead
  of relying only on the compact session bar.
- **Review host** returns to Host Vault without retaining or replaying a
  password or passphrase.
- **Close tab** removes only the failed terminal tab.
- Logs contain no password, passphrase, private-key content, clipboard content,
  or terminal input.
