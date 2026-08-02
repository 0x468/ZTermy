# ADR 0030: Explicit transfer recovery after interruption

## Status

Accepted for V1.8.

## Context

An application or machine shutdown can interrupt a queued or running transfer.
Silently reconnecting and resuming would cross authentication, conflict, and
remote-side-effect boundaries. Forgetting the operation entirely gives the user
no recovery path and can leave a deterministic temporary upload behind.

## Decision

- `transfer_recovery.json` is a versioned, size-bounded, atomically replaced
  journal beside other application data.
- The journal contains at most 100 active/interrupted task records: stable task
  and profile identifiers, display name, local/remote paths, direction, byte
  counts, and start time. It never contains credentials, passphrases, private
  keys, terminal input/output, or vault payloads.
- On startup, journal entries are surfaced as failed `interrupted` tasks. ztermy
  does not connect or transfer until the user chooses Retry.
- Retry reconstructs authentication through the current profile and credential
  vault. Locked, missing, or rejected credentials remain explicit actionable
  states.
- Upload retry removes only the deterministic `.ztermy-part-*` path derived from
  the same destination and task identifier before opening a new temporary file.
- Completed and cancelled transfers are removed from the journal. Corrupt,
  oversized, or unsupported journal data is rejected as a unit and reported
  without disabling new transfers.

## Consequences

The user can recover intent without hidden network activity. Local and remote
paths are persisted and can themselves be sensitive, so the journal belongs in
the selected installed/portable data mode and must not be treated as telemetry.
Byte-range resume is not claimed: Retry restarts the file transfer safely.

