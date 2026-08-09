# V2.9 scope: SSH connection depth and resilience

Status: approved implementation scope for `0.2.9`

V2.9 expands the native SSH connection pipeline used by terminals, SFTP,
transfers, telemetry, and later port forwarding. NetCatty remains a workflow
reference; ztermy does not copy its renderer, bridges, persistence, or assets.

## Included

- Saved profiles gain typed connection options for terminal type, bounded
  keepalive, startup commands, environment requests, proxy selection, host
  chains, and reconnect policy. Profile schema migration preserves all V1–V2.8
  profiles with conservative defaults.
- Keepalive is configured and advanced by the SSH worker without blocking the
  GUI thread. Failure becomes an explicit connection failure and never loops
  indefinitely.
- Reconnect is a bounded, visible state machine with cancellation and backoff.
  It may rebuild a terminal connection and restore viewport geometry; it never
  claims the former remote process survived and never replays raw terminal
  input, paste, or unacknowledged commands.
- Startup commands execute only after a new interactive shell reaches the
  connected state. Their execution mode and line delay are explicit and
  bounded. Environment requests and terminal type are applied while opening
  the channel.
- Windows OpenSSH agent authentication is a distinct authentication method.
  Remote agent forwarding remains a separate capability and is enabled only if
  its channel/message boundary is implemented and tested.
- Direct TCP, explicit proxy, and ProxyJump connections share one cancellable
  byte-transport abstraction. Multi-hop depth, handshake time, buffered bytes,
  and failure propagation are bounded; every hop verifies its own host key.
- The host inspector exposes supported options in compact, collapsible groups
  with inheritance-free global defaults for this personal application.

## Deliberate boundaries

- No plaintext credential fallback and no credentials inside profile JSON.
- No automatic trust of a jump host or final host, and no reuse of one host's
  decision for another endpoint.
- No automatic reconnection after an authentication rejection, changed host
  key, invalid configuration, or explicit user disconnect.
- No shell prompt scraping and no command-output heuristic for connection state.
- No Mosh, EternalTerminal, Telnet, Serial, X11 forwarding, or per-profile
  appearance in V2.9.
- Compatibility algorithm overrides are added only for algorithms supported by
  the locally pinned libssh2/OpenSSL build and require an explicit warning.

## Delivery phases

1. Typed options, schema migration, validation, and host-inspector UX.
2. Terminal type, environment requests, startup command, and keepalive.
3. Bounded reconnect state and lifecycle/UI integration.
4. Windows OpenSSH agent authentication.
5. Byte-transport abstraction, explicit proxy, and ProxyJump/host chains.
6. Direct/multi-hop real-host, migration, failure, shutdown, and Release gates.

The order is architectural: each phase must preserve direct SSH behavior before
the next transport is introduced.

## Progress

- Phases 1–2 are complete: typed schema v4 options, migration, profile editor,
  terminal type, environment requests, startup commands, and keepalive are
  implemented and tested.
- Phase 3 is complete: reconnect eligibility, bounded backoff, stable-session
  budget reset, cancellation, manual retry, localized UI states, and the
  authorized real-host reconnect gate are implemented.
- Phase 4 implementation is complete: Agent is a secret-free authentication
  method across profiles, quick connections, terminals, SFTP, transfers, and
  reconnects. The authorized host passed the explicit unavailable-agent gate.
  The opt-in success gate is defined; it remains pending on a Windows agent
  service with an accepted identity loaded.
- Phase 5 foundation is complete: all libssh2 traffic, including direct SSH,
  now uses the cancellable byte-transport callback boundary and owns the
  transport on the heap for stable session lifetime. SOCKS5 and HTTP CONNECT
  handshakes, bounded protocol parsing, cancellation, separate proxy credential
  storage, schema migration, and connection-bootstrap composition are complete
  at the native layer. Profile UX and ProxyJump composition remain.
- Phase 6 remains after phase 5 composition before V2.9 acceptance.

## Acceptance boundary

- Unit and migration tests cover defaults, malformed limits, every new state,
  secret clearing, retry eligibility, backoff, cancellation, host-key identity,
  and hop-chain validation.
- Fake transports prove partial I/O, timeouts, cancellation, keepalive, and
  reconnect without depending on a real network.
- Real-host gates cover direct password/key paths already authorized by the
  owner plus a separately configured proxy/jump fixture when available.
- UI runtime gates cover compact/regular host inspector, keyboard traversal,
  focus restoration, warnings, connecting/reconnecting/failure states, themes,
  localization, and shutdown.
- Debug and static Release builds pass the program-wide gates in
  `PRE_V3_PROGRAM.md` before the milestone is committed and packaged.
