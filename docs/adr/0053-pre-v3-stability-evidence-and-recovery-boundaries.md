# ADR 0053: Pre-V3 stability evidence and recovery boundaries

Status: accepted

## Context

By V2.13, ztermy has native terminal, SSH, SFTP, forwarding, workspace, scripts,
notes, credentials, diagnostics, and Windows shell integration. Each subsystem
has focused tests, but the final `0.2.x` release also needs a coherent answer to
two product risks:

1. an interrupted or externally damaged non-secret settings file can make an
   otherwise healthy application lose its usable configuration at startup;
2. individually passing tests do not demonstrate bounded behavior when terminal,
   network, persistence, and UI lifecycles are exercised as a release matrix.

Workspace state already keeps a validated last-known-good file. Other core
non-secret stores use atomic replacement but have no recovery copy.

## Decision

Introduce one shared bounded-file recovery primitive behind the persistence
layer. The primitive owns only byte-level atomic read/write and backup selection;
each store remains the authority that validates its schema and domain values.

The selection rules are:

- return the primary when it is fully valid;
- return a valid `.bak` payload only when the primary is missing, unreadable, too
  large, or invalid;
- fail closed on an unsupported primary schema without consulting an older
  backup;
- before saving, copy the current primary to `.bak` only if the store validates
  it as fully usable;
- never replace a valid backup with invalid or unsupported primary bytes;
- write both backup and primary through atomic replacement and enforce each
  store's existing size limit.

Application settings, SSH profiles, forwarding rules, and scripts adopt the
primitive in V2.14. Credentials are excluded because their vaults already own
authenticated encryption, rollback, and platform-specific security boundaries.
Transfer recovery remains separately owned because it represents resumable
runtime work rather than durable preferences.

A V2.14 stability target composes existing focused gates and enables the
configurable terminal soak. Runtime and physical checks remain explicit; a test
is never reported as executed merely because it exists.

## Consequences

- A damaged non-secret primary no longer forces empty/default state when a valid
  previous generation exists.
- A future-version document cannot be silently downgraded or overwritten.
- Store parsers stay independent, so the shared helper cannot accidentally
  bless domain-invalid JSON.
- One additional bounded backup generation is retained per participating store.
- Recovery can be reported without logging filenames, payloads, profiles,
  commands, or secrets.
- The release process gains a longer but reproducible stability gate; the normal
  edit/build loop remains unchanged.

## Rejected alternatives

- **Rely only on `QSaveFile`:** protects a write in progress but does not recover
  from later corruption or external truncation.
- **Keep unlimited timestamped backups:** increases privacy exposure and creates
  an unbounded retention policy for configuration content.
- **Silently reset invalid files to defaults:** produces avoidable data loss and
  hides the degraded startup condition.
- **Use the backup after a future-schema error:** risks downgrading data that an
  older binary does not understand.
- **Put credentials in the generic backup:** bypasses the credential-vault
  security and migration contracts.
