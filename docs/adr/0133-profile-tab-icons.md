# ADR 0133: Profile-owned terminal Tab icons

- Status: Accepted
- Date: 2026-09-26

## Decision

SSH profile schema 9 adds `iconName`, a stable name from the existing built-in
interface icon set: `terminal`, `hosts`, `network`, `folder`, `security`, or
`commands`. It defaults to `terminal` when loading schemas through 8. Names are
not filesystem paths, URLs or SVG payloads; invalid schema-9 values fail validation
instead of silently loading external resources or displaying an empty icon.

The host name field offers the picker and shows the current icon. Its original
auto-filled-name selection behavior is preserved in the extracted field control.
Saving uses the existing profile options map and atomic profile save; omitted
icon options preserve the stored value. Authentication and icon edits are not
separate, partially successful saves.

Terminal Tab presentation resolves the icon from its source Profile, so existing
main and detached Tabs update after a Profile edit without restarting sessions.
Workspace restoration keeps the Profile reference, not a second icon copy. Local
terminals, missing Profiles and transient connections use the terminal icon.
Connection/progress coloring remains independent of the selected icon.

## Compatibility and verification

The fixed schema-8 fixture contains a target, jump host, identity/credential
references and non-default session settings. Migration compares the complete
JSON document after removing only the newly added icon fields and resetting the
version for comparison. Store tests also cover icon round trips, unknown/future
versions, malformed types and invalid-icon saves leaving the last valid data
unchanged. This does not change application-settings or workspace schema versions.

Controller and real-QML interaction checks cover separate boundaries: Profile
edits feeding Tab models, and the six-entry picker selecting/resetting the field.
Cross-window rendered icon appearance and full mouse/keyboard acceptance remain
explicit UI checks; passing serialization tests alone does not establish them.
