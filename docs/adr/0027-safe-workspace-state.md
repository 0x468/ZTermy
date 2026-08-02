# ADR 0027: Safe per-profile workspace state

## Status

Accepted

## Context

Session productivity benefits from remembering the terminal workbench layout and
recent remote directories. Restoring a live terminal, terminal contents, input,
or authentication material would create a much larger security and lifecycle
contract. It would also make application startup capable of silently reconnecting
to a remote host.

## Decision

Store a versioned `workspace_state.json` beside the selected application settings
file. The document may contain only a saved profile identifier, the last and most
recent normalized remote paths, the preferred workbench page and side, and bounded
panel sizes.

The store uses `QSaveFile` for atomic replacement, rejects duplicate or invalid
records, and bounds recent paths to 12 entries per profile. State is applied only
after the user explicitly opens a saved SSH profile. It never opens a panel,
starts a terminal, starts SFTP, reconnects, or unlocks a credential vault.

Quick connections and local terminals have no profile identity and therefore do
not write per-profile workspace state.

## Consequences

- A saved host reuses useful layout choices without changing startup behavior.
- Workspace state can be deleted independently without affecting profiles or
  credentials.
- Future schema changes require an explicit migration or version rejection.
- Restoring live sessions remains outside this decision and requires a separate
  threat and lifecycle analysis.

