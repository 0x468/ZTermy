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

### 2026-09-29: curated Profile icon pilot

Adopt a curated subset of Tabler Icons **v3.35.0**, under its MIT license,
for additional Profile categories and recognizable OS/distribution marks.
Preserve the existing six icon IDs and all existing interface assets during
this pilot. Record upstream names in the accompanying third-party notice and
embed that notice in the application resources. Brand marks identify remote
systems only; copyright permission does not imply trademark endorsement.

Normalize the upstream 24-unit outline grid into the existing 20-unit grid
with a uniform transform and 1.5-unit effective stroke. Retain currentColor,
the existing SVG image provider, and no runtime network dependency.

Profile schema 10 expands the supported icon names. Read schema 9 unchanged
and preserve all non-icon fields; old releases must reject the future schema
rather than partially rewriting a host collection with unfamiliar icon names.
The application settings and workspace schemas are unchanged.

The pilot exposes 19 choices in a grouped keyboard-focusable grid (12 general,
7 system/platform), including the original six IDs. Verification covers a
literal schema-9 fixture with jump-host and credential references, round trips
of all new IDs, and themed raster output at 16/20/30/40 pixels. The isolated
`--profile-icons-smoke --data-dir <test-directory>` check captures the complete
popup in dark/light themes and verifies distribution selection and editor reset.
Do not use an installed release's data directory when previewing schema 10.

The fixed schema-8 fixture contains a target, jump host, identity/credential
references and non-default session settings. Migration compares the complete
JSON document after removing only the newly added icon fields and resetting the
version for comparison. Store tests also cover icon round trips, unknown/future
versions, malformed types and invalid-icon saves leaving the last valid data
unchanged. This does not change application-settings or workspace schema versions.

Controller and real-QML interaction checks cover separate boundaries: Profile
edits feeding Tab models, and the six-entry picker selecting/resetting the field.
The 2026-09-27 two-process restore check also captures the actual main Tab,
detached Tab and picker content. Both restored Tabs render the saved security
icon, and all six picker entries are visible. Evidence is under
`build/msvc-dynamic-release/test-data/window-restore-c5dd24bdac6a4baeb0842510776eb87f`
(`profile-icon-main.png`, `profile-icon-detached.png`, `profile-icon-menu.png`).
These item captures have transparent backgrounds; they establish icon geometry
and placement, not contrast against every theme. The Windows hidden-launch
harness re-presents the test window only when it has not become exposed, before
requesting a frame. Picker activation is programmatic; full physical
mouse/keyboard and theme-contrast acceptance remain separate UI checks.
