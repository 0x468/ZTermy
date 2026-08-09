# V2.10 acceptance: native SSH port forwarding

This matrix covers the owned local, remote, and dynamic SOCKS5 forwarding
jobs introduced in `0.2.10`. It is intentionally separate from terminal-tab
lifetime: closing a terminal does not stop a forwarding rule, while application
shutdown must stop and join every forwarding worker.

## Automated evidence

- Rule schema v1 persistence, validation, stable order, unknown-version
  rejection, atomic replacement, profile dependency protection, duplication,
  and opt-in auto-start are covered by domain/store and controller tests.
- Windows listener and accepted-socket tests cover loopback binding, conflict
  reporting, nonblocking accept, half-close, cancellation, and handle cleanup.
- The incremental SOCKS5 parser covers IPv4, IPv6, bounded domain names,
  pipelined payload, malformed and oversized frames, unsupported authentication,
  commands, and address types.
- Forwarding-job tests cover owned start/stop/join, bounded per-direction
  buffers, local client limits, state snapshots, and deterministic failure
  mapping.
- QML compilation, `qmllint`, `qmlformat`, English/Chinese translation
  completeness, and compact/regular light/dark Hosts layout gates include the
  Port forwarding list and empty state.

## Authorized real-host evidence

- The owner's trusted key fixture authenticated and opened two concurrent local
  `direct-tcpip` channels on one forwarding session.
- A ztermy dynamic SOCKS5 listener accepted a pipelined CONNECT request and
  returned the trusted fixture's SSH banner through the authenticated channel.
- No password, passphrase, private-key content, proxied payload, requested
  SOCKS destination, or unredacted secret-bearing command line is logged.

## Manual acceptance retained

1. In Hosts, create one local, one remote, and one dynamic rule. Verify the
   right inspector, host/type selectors, validation, broad-bind warning,
   duplicate, copy endpoint, delete confirmation, keyboard traversal, and
   outside-click dismissal in English and Chinese, both themes, and a narrow
   window.
2. Start local forwarding to a reachable service. Connect two clients, transfer
   data in both directions, observe client/byte counters, close one client, and
   verify the other remains usable. Stop must release the local port promptly.
3. Start dynamic forwarding and configure a SOCKS5-capable client. Verify IPv4,
   IPv6 when available, and a domain target; unsupported UDP/BIND/authentication
   requests must fail without stopping the rule.
4. If the SSH server permits `tcpip-forward`, start remote forwarding and
   connect from the remote side. If policy rejects it, verify a stable visible
   rejection state rather than a hang or retry loop.
5. Repeat local and dynamic forwarding through explicit proxy and jump-host
   profiles. Every unknown or changed host key must identify the exact hop;
   rejection must stop startup without silently trusting another endpoint.
6. Enable auto-start for a password/key-passphrase profile in portable mode,
   lock the vault, and restart ztermy. The rule must show that it is waiting for
   vault unlock; unlocking starts it once. Duplicated rules must not inherit
   auto-start.
7. Create a bind conflict and disconnect the network during active traffic.
   Verify actionable error state, bounded resource use, no GUI stall, and a
   successful explicit restart after the cause is corrected.
8. Close all terminal tabs while forwarding remains active, then exit ztermy
   with multiple clients connected. Shutdown must complete without a crash,
   late prompt, lingering listener, or worker process.
9. At 100%, 125%, 150%, and 200% DPI, inspect rule cards, menus, right editor,
   host-key prompt, and focus rings in compact/regular layouts.

## Release evidence

- MSVC dynamic Debug and static Release builds completed successfully.
- All 50 CTest cases passed in both presets (45.39 seconds dynamic Debug and
  56.51 seconds static Release).
- clang-format, all 115 clang-tidy translation units, `qmlformat`, `qmllint`,
  and 1,021/1,021 finished Chinese translations passed.
- The serial eight-gate real-window preflight passed, including 100%, 125%,
  150%, and 200% DPI capture.
- The authorized `testkey` real-host fixture passed two concurrent local
  channels and a pipelined SOCKS5 connection without exposing credentials.
- The self-contained artifacts are under
  `build/msvc-static-release/package/release/ztermy-0.2.10-windows-x64`:
  - portable SHA-256:
    `881e48fbf80a8a88e74e7e79878a608beea40029722a5eacad093dd49af36ca6`;
  - MSI SHA-256:
    `a867424fd06cfd9a52315034213b385f20f7afe575bb51f918335f39b2cbeedc`.
- WiX produced the MSI, but ICE contract validation could not run because the
  Windows Installer service is disabled/unavailable on this workstation. The
  service was not changed. This is an environment caveat, not an ignored ICE
  diagnostic; MSI install/uninstall remains in the retained manual matrix.
