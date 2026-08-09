# V2.9 acceptance: SSH connection depth and resilience

## Automated evidence

- MSVC dynamic Debug configured and built successfully; CTest passed 46/46 in
  44.35 seconds.
- MSVC static Release built successfully; CTest passed 46/46 in 38.26 seconds,
  including native executable version and icon metadata for `0.2.9`.
- C++ formatting, QML formatting/qmllint, the 945-pair translation gate, and
  full clang-tidy analysis of 103 translation units passed with diagnostics
  treated as errors.
- Schema migrations through profile schema v6 cover conservative defaults,
  malformed limits, missing/self/duplicate jump references, persisted order,
  dependent-profile deletion protection, and credential separation.
- Scripted transports cover partial I/O, timeout, cancellation, SOCKS5 and HTTP
  CONNECT negotiation, bounded parsing, and post-handshake bytes.
- Request/bootstrap/session tests cover terminal options, keepalive, bounded
  reconnect, agent-unavailable behavior, secret clearing, host-key endpoint
  identity, and nested `direct-tcpip` ownership and shutdown.

## Authorized real-host evidence

- Direct key authentication passed against the owner's dedicated trusted test
  fixture.
- A temporary local OpenSSH SOCKS5 forward exercised ztermy's explicit proxy
  transport through host-key verification and key authentication. The helper
  process was terminated in the test command's cleanup block.
- A complete two-layer bootstrap authenticated the first hop, opened a native
  `direct-tcpip` channel, independently verified the inner endpoint, and
  authenticated the final session. Host-key callbacks distinguished the jump
  endpoint from the final endpoint.
- The same resolved route is used by terminal, SFTP, background transfer, and
  reconnect request providers; target, proxy, and every hop secret are cleared
  by request guards.

## Distribution evidence

- The self-contained portable archive and per-user MSI were generated.
- WiX decompilation and static contract inspection passed: per-user scope,
  LocalAppData installation, one `ztermy.exe`, product icon, direct Start-menu
  shortcut, same-version upgrade, uninstall folder removal, and absence of
  portable flags, DLLs, PDBs, and Ghostty payloads.
- WiX ICE execution could not run because this machine's Windows Installer
  service is disabled/unavailable. This is retained as an environment gate and
  is not misreported as an MSI schema pass.
- Release artifacts:
  - `ztermy-0.2.9-windows-x64-portable.zip`: 19,009,697 bytes,
    SHA-256 `225a660fc7c18661c7f3b89b81d2953f600e50dc2fa4bf5de01fc35e3db650d9`;
  - `ztermy-0.2.9-windows-x64.msi`: 15,380,480 bytes,
    SHA-256 `a80face476ba4271eaec1e723b993ac6e2cfe5e6588bc94bfa2c0652c97e0d64`.

No password, passphrase, private-key content, terminal input, or unredacted
secret-bearing command line is recorded in this document or application logs.

## Manual acceptance retained

1. In compact and regular host editors, configure terminal type, keepalive,
   reconnect, startup commands, environment values, explicit proxy, and a
   two-hop route. Verify keyboard traversal, focus, localized warnings, and
   light/dark layout without clipped controls.
2. Connect through every configured route. Every unknown/changed host-key
   prompt must name the exact hop; rejecting any hop must stop the route without
   trusting the remaining endpoints.
3. Disconnect the network during an eligible session. Verify visible bounded
   reconnect/backoff, cancellation, manual retry, and no replay of terminal
   input or startup commands beyond the documented new-session behavior.
4. With Windows OpenSSH Agent running and the accepted key loaded, verify Agent
   authentication for terminal and SFTP. Repeat with the agent stopped and
   confirm the deterministic unavailable-agent state without a password prompt.
5. Close a tab and then the application while direct, proxy, and multi-hop
   connections are active or reconnecting. Shutdown must complete without a
   crash, lingering helper, late prompt, or secret-bearing log entry.
6. Run the eight serial native-window gates at 100%, 125%, 150%, and 200% DPI.
   Inspect host editor, host-key prompt, reconnect state, terminal, and shutdown
   in both themes and both supported languages.
7. On a machine where Windows Installer ICE is available, run the installer
   contract target, then test install, same-version upgrade, launch, and
   uninstall of the per-user MSI.
