# ADR 0114: Separate reusable SSH identities from host profiles

## Status

Accepted for the N2 keychain milestone.

## Context

The existing Credentials workspace is a projection of per-profile authentication fields. It repeats the same password
or private-key path for every host and cannot create, import, inspect, or reuse keys. Netcatty's current keychain instead
models keys/certificates separately from identities, then lets hosts reference an identity. Ztermy needs that product
behavior without copying Netcatty source, assets, or storage formats.

Existing profile and credential files are durable user data. Profile schema 7 stores username, authentication method,
private-key path, passphrase requirement, and an optional credential reference. The active credential vault stores the
secret under that reference. Migration must keep old profiles connectable even if keychain migration cannot finish.

## Decision

Ztermy owns a versioned `keychain.json` catalog with two record types:

- a key record describes a generated, imported, or externally referenced private key and optional public key or
  certificate file;
- an identity combines a label, username, authentication method, optional key reference, and optional vault credential
  reference.

Private-key material is never embedded in `keychain.json`. Generated and copied imports live in an application-managed
key directory; reference imports keep the user's original path. Passwords and key passphrases continue to use the active
credential-vault backend. SSH-agent identities contain no stored secret or private-key path.

Profile schema 8 adds an optional identity reference. Legacy authentication fields remain readable as a fallback and for
rollback compatibility, but a referenced identity is authoritative when resolving a connection. The migration is
idempotent: it creates a per-profile identity only for legacy profiles that have authentication material, copies the
existing credential to the identity reference before switching the profile, saves the catalog and profiles atomically at
their individual file boundaries, and retains the old vault credential until the new profile document is durable.

The application layer exposes keychain operations through a dedicated controller. File parsing, copying, key generation,
and persistence run outside the GUI and Qt Quick render threads. Host and proxy connection code receives a resolved
authentication snapshot; it does not depend on QML or keychain presentation types.

Deleting a key that an identity uses, or deleting an identity that profiles use, requires an explicit reassignment or
unlink decision. The first implementation rejects referenced deletion and reports the referencing items; it does not
silently rewrite hosts.

## Consequences

- The workspace becomes a real “Keychain” with keys, certificates, identities, search, and reference visibility.
- One identity can be shared by multiple hosts, and identity edits take effect without duplicating secrets.
- Old profile documents remain readable; unknown future keychain or profile schemas fail without rewriting data.
- Managed keys add lifecycle work: unique filenames, bounded file sizes, cleanup only after reference checks, and
  import/generation failure recovery.
- A later platform backend may generate keys without `ssh-keygen`, but N2 may use the Windows OpenSSH client when present
  and must expose unavailability instead of blocking or pretending success.

