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
- session cleanup after incomplete handshakes.

The `ssh-host-key` test verifies:

- exact host, port, algorithm, and key matching;
- unknown hosts and ports;
- changed keys and algorithms;
- malformed records; and
- OpenSSH-style SHA-256 fingerprint formatting.

Run the focused tests through the configured Debug preset:

```powershell
ctest --test-dir build/msvc-dynamic-debug -R "windows-tcp-socket|ssh-handshake|ssh-host-key" --output-on-failure
```

The full Debug and static Release test suites must also pass before changes are
committed.

## Real-host pre-authentication gate

`ssh-real-host` is skipped unless `ZTERMY_TEST_SSH_HOST` is set. When enabled,
it connects and performs the SSH handshake, extracts the public host key, and
prints only its algorithm and SHA-256 fingerprint. It does not send a username,
password, passphrase, or keyboard-interactive response.

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
