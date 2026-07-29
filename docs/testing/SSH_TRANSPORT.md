# SSH transport testing

Status: non-blocking connection, handshake, and strict host-key classification
automated; pre-authentication real-host validation available

## Automated coverage

The `windows-tcp-socket` test uses loopback only. It verifies:

- successful connection to a local listener;
- bounded failure for a closed local port;
- pre-requested cancellation;
- prompt interruption of a pending socket read by an application command event;
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
authentication is blocked while the host is unknown, that a temporary
same-endpoint and same-algorithm record with a different public key is classified
as changed and keeps authentication blocked, then opens the authentication gate
only after an exact in-memory trust match. The temporary record is never saved.
A zero timeout prevents the trusted-gate test from sending its synthetic
credentials.

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

The later application and lifecycle gates cover accepting an unknown key,
matching persisted trust, authentication, remote close, and repeated
connect/disconnect cycles. Passwords, passphrases, private key contents,
keyboard-interactive responses, and terminal input must never appear in the
test output or application log.

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
The default, network-independent coverage also:

- verifies all SSH failure kinds retain distinct user-visible status text;
- sends unique password and private-key passphrase sentinels into failing
  worker requests; and
- captures Qt messages and emitted statuses to prove neither sentinel is
  exposed.

## Interactive input latency gate

Use a saved test profile or an interactively supplied credential. Do not put a
password, passphrase, or terminal command in a command-line argument,
environment variable, test report, or log.

1. Start the dynamic Debug build and connect to a low-latency test SSH host.
2. Type and edit enough commands to enqueue at least 100 individual key-input
   events. Include cursor movement and insertion in the middle of a line.
3. While connected, resize the window and run a command that produces several
   thousand output lines.
4. Close the SSH tab normally, then inspect the newest Debug log for `SSH
   session metrics`.

Expected:

- Input remains responsive while the remote channel is otherwise idle.
- Resize and close do not wait for the former 25-millisecond read polling
  interval or produce an assertion.
- `inputQueueSamples` is at least 100 and `inputQueueP95Us` is no greater than
  `16000`.
- P50, P95, P99, and maximum values are numeric and non-negative.
- No terminal input, credential, clipboard content, private-key content, or
  secret-bearing command line appears in the log.

The input queue metric ends when the SSH worker dequeues the command. It does
not measure network round-trip time, remote echo latency, shell execution, or
rendering.

## Repeated lifecycle stress gate

The opt-in test reuses one `SshTerminalSession` after an initial trusted-host
warm-up, then performs 20 authenticated connect/disconnect cycles. It verifies
that every cycle reaches both connected and disconnected states, never prompts
for the already remembered host key, and does not accumulate process handles.

Set only the host identity and private-key path; never put a passphrase or
private-key content in an environment variable:

```powershell
$env:ZTERMY_TEST_SSH_STRESS = "1"
$env:ZTERMY_TEST_SSH_HOST = "192.168.1.25"
$env:ZTERMY_TEST_SSH_USERNAME = "testkey"
$env:ZTERMY_TEST_SSH_PRIVATE_KEY = "$HOME\.ssh\id_ed25519"
$env:ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT = "SHA256:<trusted fingerprint>"
.\build\msvc-dynamic-debug\ztermy_ssh_terminal_session_tests.exe `
  survivesRepeatedConnectDisconnectCycles
```

Expected:

- The initial warm-up asks for host-key confirmation once and matches the
  independently trusted fingerprint.
- All 20 subsequent cycles authenticate without another trust prompt.
- Each stop joins the worker and reaches `Disconnected`.
- Final process handle count is no more than four above the post-warm-up
  baseline; linear handle growth fails the test.
- No credential, private-key content, or terminal input appears in output or
  logs.

## Failure recovery UI

Exercise name-resolution failure, connection refusal, timeout,
authentication rejection, and remote close through non-sensitive test
endpoints.

Expected:

- During resolving, connecting, handshaking, host verification,
  authentication, and channel setup, a loading panel reports the current phase
  instead of showing a failure.
- **Cancel connection** closes only the pending SSH tab and returns promptly.
- Name-resolution failure, connection refusal, timeout, authentication
  rejection, unavailable authentication, channel failure, and remote close
  retain distinct user-visible status text rather than collapsing to a generic
  connection error.
- A remote shell that exits normally is presented as the neutral **SSH session
  ended** state, not as a red authentication or transport error. It still
  offers **Close tab** and **Review host** recovery actions.
- Each failure produces its distinct non-secret status message.
- A failed SSH tab shows a visible recovery panel with the same status instead
  of relying only on the compact session bar.
- The recovery panel uses the shared error-state geometry, announces an alert
  description, and remains readable in Dark and Light themes.
- **Review host** returns to Host Vault without retaining or replaying a
  password or passphrase.
- **Close tab** removes only the failed terminal tab.
- Logs contain no password, passphrase, private-key content, clipboard content,
  or terminal input.
