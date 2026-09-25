# ADR 0129: Terminal titles have separate automatic and manual ownership

- Status: Accepted
- Date: 2026-09-26

## Decision

OSC 0/2 titles are transient per-session snapshot metadata. They never call
the manual rename API or update restore intents. Display precedence is manual
name, then the active pane's program title when enabled, then the Shell/Profile
default. An empty program title falls back to the default.

The `allowTerminalTitleChanges` application preference defaults to true and
is stored in application settings schema 39. Disabling it suppresses program
titles, not manual names. Clearing a manual name explicitly removes its pin;
changing the preference cannot remove the pin. Resetting settings refreshes
title presentation just like editing the preference.

Workspace schema 9 stores `manualTitle` separately on workspaces and restore
intents. Empty means automatic. Existing `title` retains the default label.
Renaming a tab applies its manual name to existing panes, preserving the
existing tab-wide rename behavior. Extracting a pane carries its manual name
through the restore intent. A workspace name takes precedence over its active
pane's name, including after panes are merged.

Schemas through 8 do not record title origin. Their names migrate as fixed
names rather than risking loss of a user-assigned label; clearing unlocks
them. Local/SSH restoration resolves the underlying default from the saved
Shell/Profile. Program titles are never restored as explicit user choices.

Program titles are bounded by the engine's 4096-byte UTF-8 snapshot limit;
visible labels are single-line and limited to 256 UTF-16 code units. Labels
and tooltips use plain text rather than interpreting terminal output as rich
text. Title-only output must publish a snapshot and coalesced UI notification
without persisting the workspace.

## Verification

Protocol tests cover fragmented OSC termination and clearing. Controller tests
cover preference changes, pinning, new program titles, clearing, restoration,
active-pane changes and keeping transient titles out of the workspace store.
Migration tests use fixed prior-schema documents. The opt-in lifecycle path
`ZTERMY_TEST_TITLES=1` exercises real local Shell output through ConPTY, the
terminal engine and controller, including manual override and unpinning.
