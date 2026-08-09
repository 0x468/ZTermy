# V2.9 explicit proxy evidence

Status: implementation slice complete; retained as part of the V2.9 acceptance
record.

## Automated evidence

- SOCKS5 and HTTP CONNECT scripted transports cover partial reads/writes,
  anonymous and authenticated negotiation, rejection, malformed replies,
  bounded HTTP headers, timeout, cancellation, and preservation of bytes after
  the CONNECT header boundary.
- Profile schema v5 migration, controller persistence, target/proxy credential
  separation, credential replacement/removal, rollback, and absence of secret
  bytes in `profiles.json` are covered by unit tests.
- The profile editor QML compiles ahead of time and passes qmlformat, qmllint,
  localization, keyboard/window smoke, light/dark theme, and application
  controller gates.
- The dynamic Debug suite passed all 46 registered tests after the integration
  work.

## Authorized real-host evidence

The test harness started a hidden, temporary OpenSSH dynamic forward on
`127.0.0.1:21080`, connected ztermy's byte transport through that SOCKS5
endpoint, verified the configured server fingerprint, and authenticated with
the owner's existing test key. The forwarding process was terminated in the
test command's `finally` block.

The real-host test remains opt-in through `ZTERMY_TEST_SSH_PROXY_TYPE`,
`ZTERMY_TEST_SSH_PROXY_HOST`, and `ZTERMY_TEST_SSH_PROXY_PORT`; ordinary CTest
runs skip external access.

## Subsequent ProxyJump evidence

- Profile schema v6 migration, ordered route validation, missing/self/duplicate
  rejection, deletion protection, controller projection, and route clearing are
  covered by automated tests.
- A complete real bootstrap connected to the authorized host, authenticated it
  as the first jump, opened `direct-tcpip` back to the host, independently
  verified the inner SSH identity, and authenticated the final session. The
  callback evidence distinguished `Real jump — user@host:22` from the final
  `user@host:22` endpoint.
- The same resolved route is supplied to interactive terminals, independent
  SFTP sessions, and retryable background transfers. Every request guard clears
  target, proxy, and hop secrets.

## Final status

The complete automated V2.9 evidence and retained environment-dependent checks
are recorded in `V2_9_ACCEPTANCE.md`.
