# ADR 0004: Use an application-owned versioned known-host store

- Status: Accepted
- Date: 2026-07-29

## Context

ztermy must never authenticate before the server host key has been checked.
Unknown keys require explicit user confirmation, matching keys should reconnect
without another prompt, and changed keys must stop the connection.

Reusing the user's OpenSSH `known_hosts` file would couple ztermy behavior to
external configuration, hashing modes, file permissions, concurrent writers,
and entries managed by other tools. Writing that file would also mutate user
state outside ztermy.

## Decision

ztermy will keep a separate `known_hosts.json` file in its application data
directory. Version 1 stores only public data:

- host;
- port;
- stable SSH algorithm token; and
- base64-encoded raw host key.

The persistence layer validates size and schema limits, rejects duplicate
host-port-algorithm records, and writes through `QSaveFile` so replacement is
atomic. Missing files represent an empty trust store. Malformed or unsupported
files are errors and never silently become an empty store.

Host trust remains domain logic. The persistence layer only loads and saves
`KnownHostEntry` values; it does not decide whether authentication may proceed.

## Consequences

- ztermy does not read or modify the user's OpenSSH trust database.
- Stored host keys are public and do not require credential encryption.
- An explicit import/export feature can be added later without changing the
  runtime trust invariant.
- Schema changes require a versioned migration.
- A corrupt file blocks trust loading and must be surfaced to the user instead
  of weakening verification.
