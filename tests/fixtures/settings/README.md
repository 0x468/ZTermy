# Settings schema 38

`schema-38.json` is a fixed document using the schema-38 field set and tokens,
with non-default restoration, appearance, shortcut and provider preferences.
It contains only synthetic values and opaque credential references.

Do not regenerate it from the current settings writer: its purpose is to
detect changes that accidentally stop reading or preserving the previous
format. The migration test compares every old JSON field after rewrite,
excluding only the schema number and newly introduced fields.

## Settings schema 42

`schema-42.json` preserves the previous real field set, including the global
summon shortcut and cursor-screen preference. Schema 43 adds
`windowsIntegration`. The migration compares every previous field after rewrite;
the fixture uses synthetic data and must not be regenerated from the new writer.

## Settings schema 46

`schema-46.json` is the previous real typography/settings contract. Schema 47
changes shortcut override values from strings to arrays. Its migration test
checks every unrelated JSON field, including opaque credential references,
and preserves explicit empty/unbound overrides as empty arrays.
