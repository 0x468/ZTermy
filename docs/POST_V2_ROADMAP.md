# Post-V2 roadmap

Status: proposed execution order after the accepted V2 0.2.0 baseline

## Versioning rule

Daily-use stabilization and compatible feature work remain on `0.2.x`.
`0.3.0` is reserved for an explicitly approved V3 scope; it is not the next
automatic version after this release.

Release codenames follow the `x.y` line rather than individual patch builds.
All `0.2.z` releases retain the codename `此` and its approved verse; a new
codename is selected only when the minor version changes.

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

## V2.2 — NetCatty workflow convergence (`0.2.2`)

- correct the post-V2 product-shell drift using a frozen NetCatty runtime
  baseline and the scope in `V2_2_SCOPE.md`;
- rebuild Hosts as a compact browse-and-connect workspace with a unified
  command row, compact host items, secondary management actions, and the
  established right-side editor;
- converge the terminal toolbar, workbench, command snippets, composer, find,
  session logging, and integrated SFTP interaction anatomy;
- replace presentation-oriented settings cards with compact setting rows while
  retaining the approved on-demand Settings work tab;
- add SFTP productivity only where persisted behavior and automated evidence
  can be completed in the same release.

## V2.3 — compatibility, accessibility, and deferred productivity (`0.2.3`)

- execute the retained high-contrast, Narrator, keyboard-only, physical DPI,
  multi-monitor, and long-running shutdown matrix;
- improve startup, resize/reflow, large-history search, SFTP listing, and
  transfer-center performance using measured budgets;
- evaluate tree-view SFTP, Explorer-to-local drag/download, true script
  triggers, session telemetry, and unsupported transfer pause/resume contracts;
- finish English/Chinese visual QA and migration/upgrade coverage for all
  supported `0.2.x` data.

The frozen execution boundary is recorded in `V2_3_SCOPE.md`: path bookmarks
and empty remote-file creation ship because their persistence and transport
contracts are complete; tree SFTP, Explorer drag-out, script triggers, remote
telemetry, and transfer pause/resume remain deferred until their backend
contracts can be implemented without placeholder UI.

## V2.4 — SFTP navigation continuity (`0.2.4`)

- consume standard terminal current-directory reports without shell command
  injection or prompt scraping;
- add explicit locate and opt-in follow-terminal-directory actions;
- add a real lazily loaded SFTP tree with independent node requests and
  localized failures;
- persist list/tree and follow preferences in workspace schema v4.

The frozen boundary and backend contract are recorded in `V2_4_SCOPE.md` and
ADR 0039. Explorer drag-out, script triggers, telemetry, and resumable transfer
controls remain deferred.

## V3 decision gate (`0.3.0`)

V3 starts only after the owner chooses a coherent major direction and records
an ADR/scope. Candidate directions include Linux portability, a standalone
file-management workspace, or deeper remote-development workflows. Cloud sync,
collaboration, AI, serial support, and remote editing remain separate decisions
and are not implied by V3.

After V2.4 acceptance, daily-use defects may continue on `0.2.x`. The next
planned feature milestone is chosen at the V3 decision gate rather than by
automatically incrementing to `0.3.0`.
