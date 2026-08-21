# ADR 0101: Application settings schemas are monotonic

- Status: Accepted
- Date: 2026-08-21

## Context

An experimental build wrote application settings with schema version 20. The
external-Agent option that introduced that version was later removed and the
current schema was accidentally reduced to 19. Existing settings then appeared
to come from an unsupported future version, so the whole document was rejected.
Unrelated AI provider URL, model, and credential-reference settings appeared to
have been lost and subsequent saves were refused.

The schema number describes documents already written to user storage. It is a
durable compatibility contract, not the number of fields currently present in
the C++ structure.

## Decision

- Application settings schema numbers only increase. They are never
  decremented, reused, or renumbered after any development or release build has
  written them.
- Removing a feature leaves its schema version as a readable tombstone. Removed
  members are ignored on load and omitted on the next save.
- Every schema change adds a fixture representing the immediately preceding
  real document and verifies load, preservation of unrelated settings, and
  rewrite to the current version.
- Tests assert the exact current schema number. Changing it requires updating
  migration coverage in the same change, so an accidental rollback fails CI.
- Unknown future schemas remain read-only failures. An older build must not
  silently rewrite a document containing fields it cannot understand.
- Secrets remain stored outside `settings.json`; migrations preserve their
  opaque credential references.

## Consequences

- Schema 20 remains a readable tombstone even though `aiAgent` no longer
  exists. Schema 21 adds the native ChatGPT-subscription provider preference,
  and schema 22 adds AI-only proxy configuration.
- Provider, appearance, terminal, and credential-reference settings survive
  removal of unrelated experiments.
- The next persisted option must use schema 23 or greater and include a
  schema-22 migration test.
- Compatibility is explicit and testable instead of depending on which fields
  happen to exist in the current UI.
