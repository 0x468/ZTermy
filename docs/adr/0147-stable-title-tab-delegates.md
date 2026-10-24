# ADR 0147: Keep title-tab delegates stable across status updates

- Status: Accepted
- Date: 2026-10-25

## Context

The title strip consumed a fresh QVariantList/JavaScript array whenever terminal
titles or progress changed. Replacing that model destroys delegates, including
the MouseArea holding an in-progress click. A stable native client hit region
does not protect the press/release sequence from that destruction.

## Decision

- Keep the controller's existing snapshot API for its other consumers.
- Project the main title strip through a small UI-owned QAbstractListModel keyed
  by workspace ID. Title, progress and other field changes emit dataChanged;
  insert, remove and move notifications describe actual structural changes.
- Preserve normal release-based activation, double-click and reorder semantics.
  Do not work around lost clicks by activating on press or throttling titles.
- Size the strip from the source list rather than a projection's intermediate
  row count, so model reconciliation cannot collapse its input viewport.

## Verification

The isolated runtime fixture checks HWND client hits and Qt platform-queue mouse
delivery with title refreshes between press and release. A reset-based model
control must lose its delegate and click; the stable model must preserve both.
The same test covers maximized, full-screen and restored windows with equal and
active-title widths. Model tests cover title/progress changes and persistent
indices through insertion, movement and removal. No external Agent is launched.
