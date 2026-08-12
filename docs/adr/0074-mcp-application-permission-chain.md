# ADR 0074: MCP application permission chain

Status: accepted

## Context

ADR 0073 defines the transport boundary for a local MCP stdio server. A safe
transport is not sufficient by itself: discovered tools must pass through the
same target, approval, replay, cancellation, audit, and lifetime controls as
native AI tools. Persisting a server also must not silently persist a credential
or turn future schema changes into authority.

## Decision

- Persist at most eight local stdio server definitions in a versioned
  last-known-good JSON file. Store the stable id, namespace, absolute executable,
  bounded argument list, optional absolute working directory, trust tier,
  enabled state, and exact tool-schema approvals. Do not store credentials.
- Keep server id immutable while editing. A different identity is a new server,
  which prevents an edited record from inheriting the old server's authority.
- Start enabled discovery/execute servers asynchronously and expose explicit
  disabled, starting, ready, and error states. Restart and removal stop the
  process, revoke definitions, and resolve pending calls.
- Show every discovered tool's full description, JSON schema, and digest in
  Settings. A server must have execute trust and the exact current digest must
  be approved before its definition can enter an AI request. Description or
  schema drift revokes that approval.
- Treat every MCP call as a side-effecting, high-risk action in V3. The user must
  explicitly approve the current call even when the server and schema were
  previously trusted. Recheck the active terminal generation, server identity,
  and schema digest at approval time.
- Use the native AI dispatch ledger keyed by conversation, turn, and tool-call
  id. Duplicate calls return the retained result and never redispatch the MCP
  operation.
- Propagate cancellation to the client with `notifications/cancelled`; also
  resolve the local waiter immediately. Bound returned data to a 48 KiB
  untrusted JSON envelope with original byte count and truncation metadata.
- Record configuration changes and tool calls through the existing AI activity
  surface. MCP descriptions, schemas, errors, and results are untrusted evidence
  and cannot grant permissions.
- Do not implement server-originated elicitation, sampling, roots, HTTP/SSE, or
  OAuth in this baseline. Unknown server requests convey no authority and are
  not surfaced as user prompts.

## Consequences

- Three independent decisions are visible: trust the executable for discovery
  or execution, approve an exact tool schema, and approve each actual call.
- A malicious local server still has the OS access of its process identity;
  ztermy is not an operating-system sandbox.
- The minimal inherited environment reduces accidental credential exposure but
  is not a credential-injection mechanism. A future credential flow or remote
  transport requires its own threat model and ADR.
- Malformed configuration can recover from the previous valid backup, while a
  newer schema version is preserved and refused rather than overwritten.

