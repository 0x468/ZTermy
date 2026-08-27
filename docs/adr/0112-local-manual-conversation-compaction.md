# ADR 0112: Local manual conversation compaction

## Status

Accepted for the first `/compact` version.

## Context

Automatic request compaction protects provider context limits but gives users no
way to deliberately shorten a long conversation before the limit is reached. A
provider-generated summary would add latency, cost, failure modes, and another
model turn to a keyboard command that should complete immediately.

## Decision

- `/compact` creates a provider-neutral local checkpoint from older visible
  conversation turns and keeps the four most recent messages verbatim.
- The visible transcript is not deleted or rewritten. Future provider requests
  replace the compacted prefix with the checkpoint inside that terminal tab.
- The checkpoint is explicitly labelled as lossy, bounds every retained excerpt,
  and does not claim semantic completeness.
- Repeating `/compact` folds the previous checkpoint and newly accumulated older
  turns into a new checkpoint.
- The first version is scoped to the live terminal-tab conversation. Restoring an
  encrypted conversation starts from its full transcript and requires a new
  explicit `/compact` action.
- Automatic typed request compaction and provider-overflow recovery remain the
  final request-size boundary after manual compaction.

## Consequences

Manual compaction is immediate, deterministic, offline, and does not consume API
quota. It is intentionally less semantically rich than an LLM summary, so the UI
reports condensed item count and estimated request size instead of presenting the
checkpoint as an authoritative summary.
