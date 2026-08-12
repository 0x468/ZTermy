# ADR 0073: MCP trust, namespace, and stdio transport

Status: accepted

## Context

An MCP server is executable third-party code whose tool descriptions, schemas,
results, errors, and elicitation can be hostile. Treating discovery as approval
would allow a changed server to inject new instructions or capabilities into an
AI turn. Launching through a shell or inheriting the full environment would add
unnecessary command-injection and credential exposure.

## Decision

- Identify every server by a stable id and isolated namespace. Expose approved
  tools only as `mcp__<namespace>__<tool>`; never merge remote names directly
  into the native ztermy tool namespace.
- Separate disabled, discovery-only (`observe`), and executable trust. Only an
  executable server with an individually approved description/schema fingerprint
  contributes provider tool definitions or accepts calls.
- Revoke approval whenever a description or input schema changes. Keep server
  text prefixed as untrusted and do not let annotations grant permissions.
- Implement bounded MCP JSON-RPC over stdio with initialize, initialized,
  tools/list, and tools/call. Limit buffers, messages, arguments, schemas, tool
  count, pending calls, names, and descriptions.
- Launch a configured absolute executable directly through `QProcess`, never a
  command shell. Inherit only a small operational environment (`PATH`, Windows
  root, temp, and user-profile paths); credentials require a separate secret
  path and are not encoded in arguments or environment by this transport.
- Treat results and errors as untrusted evidence. Stopping/disabling a server
  immediately removes its definitions and fails pending calls.

## Consequences

- MCP discovery and schema drift are visible review events, not implicit trust.
- A compromised server remains capable of whatever its OS process identity can
  access; ztermy does not claim process sandboxing. Users must select server
  executables deliberately.
- Remote HTTP/SSE and OAuth transports remain outside this first V3 transport;
  adding either requires a separate credential and origin policy ADR.
- Application-level approval, audit, deduplication, and cancellation wrap this
  transport before tools become available to normal AI turns.
