# ADR 0006: Store SSH profiles without credentials

- Status: Accepted
- Date: 2026-07-29

## Context

ztermy needs reusable host entries for quick connection. A host entry contains
useful non-secret settings, while passwords, private-key passphrases, private
key contents, and agent credentials are security-sensitive.

The first V1 implementation is Windows 11-only and does not yet depend on a
platform credential vault.

## Decision

Persist SSH profiles as a versioned JSON document under Qt's per-user
`AppDataLocation`.

Each profile may contain:

- a generated stable identifier;
- display name, host, port, and username;
- authentication method;
- a private-key file path when private-key authentication is selected.

The profile store never accepts or serializes passwords, private-key
passphrases, private key contents, or other credential material. Those values
must be requested for an individual connection attempt and held in memory only.

Writes use `QSaveFile` so replacement is atomic. Loads reject malformed,
oversized, unsupported-version, duplicate-ID, or semantically invalid data.

## Consequences

- Profiles are portable and easy to inspect or back up.
- The private-key path reveals local filesystem structure but is not itself a
  credential; users can omit saving a profile and connect directly.
- Password and encrypted-key support can share the same profile schema without
  introducing secret persistence.
- A future credential-vault integration requires a separate ADR and must store
  only an opaque credential reference in the profile document.
