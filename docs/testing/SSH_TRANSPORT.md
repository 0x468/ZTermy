# SSH transport testing

Status: non-blocking connection and handshake foundation automated; real-host
validation pending

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

Run both through the configured Debug preset:

```powershell
ctest --test-dir build/msvc-dynamic-debug -R "windows-tcp-socket|ssh-handshake" --output-on-failure
```

The full Debug and static Release test suites must also pass before changes are
committed.

## Human validation

No human validation is required for the loopback-only foundation. It does not
contact an external host or request credentials.

Real-host validation starts only after strict known-host verification is
implemented. That gate will include explicit steps for an unknown key, a
matching key, a changed key, timeout, refusal, authentication failure, remote
close, and repeated connect/disconnect cycles. Passwords, passphrases, private
key contents, keyboard-interactive responses, and terminal input must never
appear in the test output or application log.
