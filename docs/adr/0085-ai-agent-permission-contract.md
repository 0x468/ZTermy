# ADR 0085: Use four truthful Agent permission modes

Status: accepted

Date: 2026-08-14

## Context

V3 exposed five modes: read-only, ask, edit, auto, and YOLO. The implementation mapped ask and edit to the same policy, mapped auto and YOLO to the same policy, did not tell the model which mode was active, and exposed action/MCP tools in every mode. The edit label also implied a reviewable file-edit boundary that ztermy does not currently have.

## Decision

Use four modes:

- read-only: expose read tools only; action and external MCP tools are absent;
- ask: every side effect pauses for native approval;
- auto: ordinary actions run, while high-risk commands and external MCP tools ask;
- YOLO: actions do not prompt, but explicit deny rules, validation, scope, ownership, and budgets remain authoritative.

Inject the active mode contract into the system prompt, while treating client enforcement as the only security boundary. Command-suggestion requests do not receive action or MCP tools.

Remove edit rather than aliasing or migrating it. It may return only after ztermy has a first-class reviewable edit/diff/patch capability with a distinct safety boundary.

## Consequences

- Mode labels correspond to observable behavior.
- Auto and YOLO are now meaningfully different.
- A stored `edit` preference is intentionally invalid under the new settings contract; no compatibility alias is retained.
- High-risk classification changes execution policy instead of merely changing card color.
- Tests must cover both model-visible prompt behavior and client-enforced decisions.

