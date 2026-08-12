# ADR 0082: Mode-consistent MCP Agent dispatch

Status: Accepted

## Context

MCP executable trust and exact-schema review decide whether a tool may be
published to a model. They do not decide whether every invocation must stop for
another confirmation. Treating all reviewed MCP calls as permanently
ask-before-run made Auto and YOLO behave like Ask, and prevented a user from
remembering an exact MCP-tool decision for a session, Profile, or globally.

## Decision

- Keep executable trust and exact-schema review as prerequisites for publishing
  an MCP tool. Permission modes cannot bypass those prerequisites.
- Classify a published MCP invocation as the `mcp-tool` permission capability,
  with its fully namespaced exposed tool name as the rule subject.
- Apply the same rule precedence used by native Agent writes:
  explicit deny, explicit ask, explicit allow, then the active mode default.
- Read-only denies MCP writes; Ask and Edit request approval; Auto and YOLO
  dispatch automatically unless an ask/deny rule matches.
- The approval card supports exact/prefix/glob/regex/all rules and once,
  session, Profile, or global duration. Session rules stay in memory; Profile
  and global rules use the existing permission-rule store.
- Replay admission happens before permission/budget consumption. A completed
  duplicate returns the cached result and never invokes the MCP process again.
- Automatic calls retain the same bounded turn budget, dispatch ledger, schema
  identity check, cancellation handle, audit activity, and untrusted result
  envelope as manually approved calls.

## Consequences

Auto and YOLO now operate without accidental per-call MCP prompts after the
owner has trusted the executable and reviewed the exact schema. Ask remains the
default interactive mode, and an explicit ask rule can reintroduce confirmation
for a selected tool even under Auto or YOLO. Edit stays conservative for MCP
because an arbitrary external tool cannot be proven to be an owned workspace
edit from its schema alone.

