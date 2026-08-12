# ADR 0067: User-approved AI runbooks

Status: superseded by ADR 0080

## Context

An operations conversation can produce a useful sequence of commands, but an
assistant must not keep that sequence as opaque provider memory or silently
turn a suggestion into a persistent executable asset. Runbooks also have to
obey the same validation, localization, persistence, and editing rules as
scripts created by the user.

## Decision

- Publish a strict `save_runbook` action containing an exact session generation,
  name, description, shell scope, and at most 64 bounded command steps.
- Always require visible user approval, including in session-auto and
  saved-host-auto modes. Observer mode cannot propose a persistent write.
- Show the full proposed name, description, and commands in the approval card.
  Denial is a completed, replay-safe tool result; approval is still guarded by
  the conversation/turn/tool-call deduplication ledger.
- Persist an approved runbook through the existing `saveScript` domain path and
  `ScriptStore`. The first version accepts no model-provided variables or secret
  defaults and returns the owned script identifier.
- Count the save against the turn's write-action budget, but do not acquire a
  terminal write lease or consume the first-terminal-write approval. A live PTY
  is not required after approval as long as the frozen session identity and
  generation still match.

## Consequences

- Generated runbooks are ordinary ztermy scripts that users can inspect, edit,
  execute, export, or delete with existing UI and storage semantics.
- A provider cannot silently persist a script, smuggle variable defaults, or
  use runbook creation to obtain terminal control.
- Rich variables and automatic extraction from a successful command trace can
  be added later only with an equally explicit preview and secret-handling
  contract.
