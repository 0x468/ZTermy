# V2.10 scope: native SSH port forwarding

Status: approved implementation scope for `0.2.10`

V2.10 adds owned native forwarding jobs on top of the V2.9 authenticated SSH
bootstrap. NetCatty supplies the compact rule-list and right-side editor
workflow reference; ztermy owns the C++ transport, persistence, lifecycle,
validation, assets, text, and Windows behavior.

## Included

- Local forwarding listens on an explicit Windows address/port and opens one
  SSH `direct-tcpip` channel per accepted client to the configured remote
  destination.
- Remote forwarding requests an explicit server address/port and bridges each
  accepted SSH forwarded channel to a configured Windows-side destination.
- Dynamic forwarding exposes a SOCKS5 CONNECT listener. It supports IPv4,
  IPv6, and bounded domain-name requests without UDP ASSOCIATE, BIND, or local
  proxy authentication.
- Rules are global non-secret entities associated with one saved SSH profile.
  They have stable IDs, labels, ordered display, type, bind and destination
  endpoints, opt-in auto-start, and schema-versioned persistence separate from
  credentials and profile JSON.
- Every job resolves the selected profile's target, explicit proxy, jump route,
  credentials, and known-hosts boundary through the V2.9 request provider.
  Every hop is verified independently.
- The Hosts workspace gains a dedicated Port forwarding section with compact
  status rows, start/stop, duplicate/delete, error details, active connection
  count, bound endpoint copy, and a right-side create/edit inspector.
- Auto-start waits for required credentials. A locked portable vault produces
  an explicit waiting state and never falls back to plaintext or an implicit
  prompt behind the main window.

## Runtime limits

- Each active rule owns one SSH session and one joinable worker. No libssh2
  session is accessed from multiple threads.
- At most 16 rules and 32 clients per rule may be active. Listener backlogs,
  SOCKS handshake bytes, per-client buffers, total queued bytes, connect time,
  idle time, and shutdown time are bounded.
- Listeners default to `127.0.0.1`. Non-loopback local/dynamic or wildcard
  remote binds require an explicit warning in the editor.
- Every start, stop, retry, client close, authentication prompt, and application
  shutdown is generation-owned; late results from a stopped job are discarded.

## Deliberate boundaries

- No privileged-port helper, firewall-rule mutation, UPnP, NAT traversal,
  transparent proxying, UDP forwarding, SOCKS4, SOCKS5 authentication, or
  command proxy.
- No credential bytes, private-key content, proxied payload, SOCKS destination,
  or unredacted application traffic is logged.
- No rule silently follows terminal tab lifetime. A user may close every
  terminal while explicitly started forwarding remains active.
- No implicit retry after authentication rejection, changed host key, invalid
  configuration, explicit stop, or bind conflict. Eligible transport failures
  use bounded visible retry only when the rule requests it.

## Delivery phases

1. Typed rule model, validation, ordering, schema v1 persistence, migration
   policy, and tests.
2. Windows listener/accepted-socket abstraction plus bounded SOCKS5 parser.
3. libssh2 local and remote forwarding channel primitives and fake-transport
   tests.
4. Forwarding job/manager lifecycle, request-provider reuse, prompts,
   auto-start, and shutdown.
5. Hosts workspace list/editor, localization, keyboard/accessibility, compact
   layout, and error/recovery states.
6. Direct, proxy, jump, local/remote/SOCKS real-host gates and complete Debug,
   static Release, package, and retained manual acceptance.

## Acceptance boundary

- Unit tests cover malformed endpoints, duplicate IDs/order, schema versions,
  atomic persistence, listener conflicts, partial SOCKS frames, unsupported
  commands, cancellation, backpressure, client limits, and late completion.
- Integration tests prove every forwarding direction with deterministic local
  echo fixtures and prove stop/app shutdown joins all workers.
- Authorized real-host tests exercise local, remote when server policy permits,
  and dynamic SOCKS forwarding through direct and jump bootstrap paths.
- Runtime UI evidence covers empty, editing, connecting, active, stopping,
  failed, credential-locked, host-key, narrow, keyboard, light, dark, Chinese,
  English, and application-shutdown states.
