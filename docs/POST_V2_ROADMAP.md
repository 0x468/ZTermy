# Post-V2 roadmap

Status: proposed execution order after the accepted V2 0.2.0 baseline

## Versioning rule

Daily-use stabilization and compatible feature work remain on `0.2.x`.
`0.3.0` is reserved for an explicitly approved V3 scope; it is not the next
automatic version after this release.

## V2.1 — daily-use stabilization (`0.2.1`)

- use `0.2.0` as the daily driver and prioritize reproducible crashes, hangs,
  transfer corruption, credential failures, and terminal rendering regressions;
- improve crash-dump/symbol handoff and diagnostic export without collecting
  terminal input, secrets, or unredacted command lines;
- harden SFTP/transfer cancellation, tab/app shutdown, recovery, and permission
  error states under longer real-host sessions;
- polish installer diagnostics and document the elevated WiX ICE development
  requirement without changing the per-user runtime install contract;
- complete a focused icon and visual-consistency pass using owned vector assets.

## V2.2 — SFTP and session productivity (`0.2.2`)

- add path bookmarks, terminal-directory synchronization/following, richer
  columns, directory-first sorting, and the compact overflow-toolbar behavior;
- evaluate tree view and Explorer-to-local drag/download against clear Windows
  drag-and-drop and overwrite contracts;
- refine transfer pause/resume feasibility, destination reveal/copy actions,
  completed-history cleanup, and progress detail;
- expand shell-aware history, scripts, session metadata, and telemetry without
  blocking terminal I/O or silently executing commands.

## V2.3 — compatibility and accessibility (`0.2.3`)

- execute the retained high-contrast, Narrator, keyboard-only, physical DPI,
  multi-monitor, and long-running shutdown matrix;
- improve startup, resize/reflow, large-history search, SFTP listing, and
  transfer-center performance using measured budgets;
- finish English/Chinese visual QA and migration/upgrade coverage for all
  supported `0.2.x` data.

## V3 decision gate (`0.3.0`)

V3 starts only after the owner chooses a coherent major direction and records
an ADR/scope. Candidate directions include Linux portability, a standalone
file-management workspace, or deeper remote-development workflows. Cloud sync,
collaboration, AI, serial support, and remote editing remain separate decisions
and are not implied by V3.

The recommended next step is V2.1 stabilization driven by real daily use, not
immediate feature expansion or a premature `0.3.0` bump.
