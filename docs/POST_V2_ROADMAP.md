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

## V2.5 — daily-use lifecycle closure (`0.2.5`)

- bound, deduplicate, and generation-cancel SFTP tree background requests so a
  large remote tree cannot grow memory indefinitely or starve file mutations;
- close the SFTP acceptance gate before shutdown, discard queued work, and
  reject late commands with deterministic cancellation results;
- keep an atomic last-known-good backup of the non-secret workspace state,
  recover from a damaged primary file, and refuse to overwrite data written by
  a newer schema;
- exercise repeated real ConPTY tab creation/close and multi-session app
  shutdown as a release preflight gate;
- repeat the complete quality, migration, real-host, portable, and MSI handoff
  checks without expanding the visible product surface.

The frozen boundary is recorded in `V2_5_SCOPE.md`, the concurrency and
recovery decision in ADR 0040, and the evidence matrix in
`testing/V2_5_ACCEPTANCE.md`. V2.5 is intentionally a reliability milestone;
remote monitoring, transfer pause/resume, script triggers, and Explorer
drag-out remain out of scope.

## V2.6 — remote system monitoring (`0.2.6`)

Status: completed. The shipped contract is recorded in `V2_6_SCOPE.md`, the
auxiliary-channel decision in ADR 0041, and the evidence/manual matrix in
`testing/V2_6_ACCEPTANCE.md`.

- reuse the session's isolated auxiliary SSH exec channel rather than opening
  a second user-visible terminal or parsing prompt output;
- start with a Linux adapter that samples fixed, read-only commands and virtual
  files (`/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `df`, and bounded
  `ps` output), with no `sudo`, shell interpolation, or secret-bearing logs;
- show a compact CPU, memory, disk, network, and SSH-latency strip in the
  terminal identity row, with themed hover panels for history and detailed
  breakdowns;
- poll only while the SSH session is connected and its tab is visible, use a
  five-second minimum interval, bounded output/timeouts, backoff after errors,
  and suspension after three consecutive failures;
- retain a bounded in-memory ring buffer only. Fast counters use the normal
  interval; expensive process/disk details refresh more slowly or on demand;
- keep platform collectors behind an adapter boundary. Linux is the first
  supported remote OS; macOS and Windows remote collectors are later subphases
  and must not be inferred from a generic SSH connection.

V2.6 must ship a capability/error state that is unobtrusive when unsupported;
it must never inject commands into the interactive shell or block terminal I/O.

## V2.7 — terminal productivity controls (`0.2.7`)

Status: implemented. The frozen contract is recorded in `V2_7_SCOPE.md`, the
security and rendering boundaries in ADR 0042, and the verification matrix in
`testing/V2_7_ACCEPTANCE.md`.

- add persisted, host-owned literal keyword highlighting with bounded visible
  viewport matching and deterministic first-rule priority;
- add temporary per-tab terminal appearance overrides without creating a
  profile-level appearance hierarchy;
- implement real SSH transport conversion for UTF-8 and GB18030, including
  streaming remote decode across packet boundaries;
- distinguish structured script recording from session logging: only commands
  executed from trusted application command surfaces are recorded, reviewed,
  exported, and replayed;
- align the terminal action hierarchy and progressively move less-used actions
  into overflow at narrow widths while preserving tooltips and keyboard access.

V2.7 does not add arbitrary regular expressions on the paint path, raw-input
recording, a general-purpose script runtime, per-profile appearance, or terminal
video capture. Those would require separate performance and security contracts.

## V3 decision gate (`0.3.0`)

V3 starts only after the owner chooses a coherent major direction and records
an ADR/scope. Candidate directions include Linux portability, a standalone
file-management workspace, or deeper remote-development workflows. Cloud sync,
collaboration, AI, serial support, and remote editing remain separate decisions
and are not implied by V3.

After V2.7, compatible daily-use work may continue through the planned
`0.2.x` line (including richer transfer and workspace workflows). The V3
direction is still chosen explicitly at the decision gate rather than by
automatically incrementing to `0.3.0`.
