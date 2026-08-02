# ADR 0029: Versioned script-library interchange

## Status

Accepted for V1.8.

## Context

Reusable commands need a portable backup and migration format without turning
the library into executable configuration or silently replacing local data.

## Decision

- Import and export reuse the versioned JSON schema and validation owned by
  `QuickCommandStore`.
- Import merges into the existing library. Colliding identifiers are regenerated
  so no local entry is overwritten.
- The merged library is capped at 1000 validated entries and is committed with
  the same atomic persistence path as ordinary edits.
- Import never executes or inserts a command. Run and insert remain explicit,
  separate user actions.
- Export includes command metadata but no credentials, terminal history, shell
  output, or profile secrets.

## Consequences

Libraries can be backed up and shared predictably. Re-importing the same file
creates additional entries rather than guessing whether content is identical;
deduplication remains an explicit future product decision.

