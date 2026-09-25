# Settings schema 38

`schema-38.json` is a fixed document using the schema-38 field set and tokens,
with non-default restoration, appearance, shortcut and provider preferences.
It contains only synthetic values and opaque credential references.

Do not regenerate it from the current settings writer: its purpose is to
detect changes that accidentally stop reading or preserving the previous
format. The migration test compares every old JSON field after rewrite,
excluding only the schema number and newly introduced fields.
