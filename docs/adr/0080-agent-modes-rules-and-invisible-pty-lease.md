# ADR 0080: Agent modes, reusable rules, and an invisible PTY lease

Status: accepted

## Context

The first V3 permission vocabulary (`observer`, ask-each-write,
ask-first-write, session-auto, and saved-host-auto) exposed implementation
history instead of the workflows users recognize from current agent harnesses.
It also coupled a terminal serialization lease to a visible “take control”
interaction. The result was unnecessary friction for ztermy's expert, personal
use case.

At the same time, one PTY cannot safely accept independently scheduled user and
agent byte streams. Removing the visible ceremony does not remove that technical
fact. The application still needs deterministic write ordering, cancellation,
and exact target generations.

## Decision

- Replace the product vocabulary with `Read-only`, `Ask`, `Edit`, `Auto`, and
  `YOLO`.
- Treat mode as the default decision and reusable rules as typed exceptions.
  A rule records capability, matcher (`exact`, `prefix`, `glob`, `regex`, or
  `all`), pattern, decision (`allow`, `ask`, or `deny`), duration (`once`,
  `session`, `profile`, or `global`), and optional profile identity.
- Evaluate invariant failures first, then explicit deny, ask, allow, and finally
  the mode default. Rules never repair a stale target, malformed call, closed
  session, unavailable capability, or exhausted execution budget.
- Ask-mode approval exposes Run once, allow for this session, always allow,
  deny once, and always deny. The suggested matcher is editable before a
  reusable rule is stored.
- Edit mode automatically permits owned file/runbook/SFTP mutation in scope;
  terminal command and PTY writes still use rules or ask.
- Auto executes supported actions unless an explicit ask/deny rule matches.
  YOLO executes all supported actions without heuristic warning prompts.
- Keep command-risk classification only as informative metadata for Ask/Edit
  cards and audit UI. It does not override Auto/YOLO or direct visible Run.
- Retain the one-writer lease between Agent conversations as an internal
  mechanism. Direct user input always uses the existing ordered PTY input queue
  and does not cancel model orchestration merely because the user typed. The
  normal AI panel does not show Take control/Resume controls.
- Render assistant prose as Markdown, keep native tool/action cards separate,
  and copy the raw provider text.

## Compatibility and migration

Versioned settings migrate old values as follows:

| Legacy value | New value |
| --- | --- |
| `observer` | `read-only` |
| `ask-each-write` | `ask` |
| `ask-first-write` | `edit` |
| `session-auto` | `auto` |
| `saved-host-auto` | `yolo` |

The last mapping removes the saved-host restriction because target identity is
already validated independently. Users who require a host-specific exception
use a profile-scoped rule.

## Consequences

- Daily interaction matches current agent products and no longer treats the
  user as an adversary.
- Permanent behavior is inspectable and revocable instead of being hidden in a
  session boolean.
- The application retains deterministic PTY ordering without making ownership
  a user-facing tax.
- Rules add a versioned persistence and migration surface; matching and
  precedence require dedicated tests, including shell control operators and
  profile scope.
- YOLO is intentionally literal. Its remaining failures are technical
  invariants, not disguised approval prompts.

## Implementation note

The `0.3.6` implementation stores session rules only in memory and persists
Profile/global rules in a bounded, versioned `ai_permission_rules.json` file
with last-known-good recovery. Deny wins over ask, which wins over allow when
multiple rules match. A once choice approves or denies only the visible card
and is not persisted. The Settings rule manager edits matching, toggles a rule,
and revokes it without requiring a separate security workflow.
