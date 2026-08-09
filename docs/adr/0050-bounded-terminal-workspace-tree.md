# ADR 0050: Use a bounded split tree with explicit restore intents

## Status

Accepted

## Context

ztermy currently owns terminal sessions in C++ and presents one active custom
terminal item. V2.11 needs persistent split panes without turning terminal cells
into QML objects, coupling domain state to Qt presentation, retaining secrets,
or implying that a local or remote process survived application exit.

## Decision

Represent each workspace as a flat, validated binary tree. Leaf nodes reference
stable restore-intent identifiers; split nodes reference two child node IDs and
store a horizontal or vertical ratio. Runtime sessions and QML terminal items
remain separate from the persisted tree.

Use these bounds:

- eight leaves and fifteen total nodes per workspace;
- 32 live terminal sessions application-wide;
- ratios from 0.20 through 0.80;
- unique bounded IDs and no cycles, orphans, duplicate leaf references, or
  missing children.

Persist only local-shell and saved-profile SSH intents. A restored local intent
starts a new shell. A restored SSH intent is disconnected until the user
explicitly reconnects. Secrets continue to come from the selected credential
vault and host keys continue through the existing trust pipeline.

Each visible leaf owns one `TerminalItem`; C++ routes snapshots to the items
showing the corresponding session. Focus selects the active pane/session, and
input is accepted only from that focused pane. Structural mutations are atomic
and operate on IDs, never on terminal cell grids.

## Consequences

- The model is deterministic, testable without QML, migration-friendly, and
  independent of platform window APIs.
- Multiple live panes cost one terminal scene-graph item each, but never one QML
  object per cell.
- Restored SSH panes require an explicit click/shortcut, avoiding surprising
  network access and locked-vault prompts at startup.
- Quick connections cannot be restored because persisting enough information
  would create a second profile/secret system.
- Native window detachment and true remote process resurrection remain future
  features rather than accidental promises.
