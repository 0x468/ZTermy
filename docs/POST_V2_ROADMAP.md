# Post-V2 roadmap

Status: proposed execution order after the accepted V2 0.2.0 baseline

## Versioning rule

Daily-use stabilization and compatible feature work remain on `0.2.x`.
`0.3.0` is reserved for an explicitly approved V3 scope; it is not the next
automatic version after this release.

Release codenames follow the `x.y` line rather than individual patch builds.
All `0.2.z` releases retain the codename `此` and its approved verse; a new
codename is selected only when the minor version changes.
The accepted `0.3.x` codename is `糸`, with
`「剪不断，理还乱，是离愁」`; final presentation is approved before the
first `0.3.0` release candidate.

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

## V2.8 — SFTP and transfer workflow closure (`0.2.8`)

Status: implemented. The frozen contract is recorded in `V2_8_SCOPE.md`, the
resume/cleanup decision in ADR 0043, and the evidence matrix in
`testing/V2_8_ACCEPTANCE.md`.

- add real byte-range pause/resume for uploads and downloads, including
  persisted paused state and explicit post-crash retry;
- retain validated deterministic partial files while paused or retryable, clean
  them on cancellation or dismissal, and atomically commit completed downloads;
- add aggregate pause/resume/cancel plus per-task path copy, download-folder
  opening, and completed-download drag-out to Explorer;
- add per-host SFTP sorting, directory-first order, visible-column preferences,
  and UTF-8/GB18030 filename conversion at the transport boundary;
- prove the workflow with real-host 64 MiB upload/download pause-resume gates,
  GUI screenshots, recovery tests, and shutdown-safe worker ownership.

V2.8 does not claim recursive directory transfer or Windows virtual-file drag
directly from an undownloaded remote entry. Those require separate queue and
`IDataObject` contracts rather than presentation-only controls.

## V2.9 — SSH connection depth and resilience (`0.2.9`)

Status: implemented. The frozen contract is recorded in `V2_9_SCOPE.md`, the
transport decisions in ADRs 0044–0048, and the evidence/manual matrix in
`testing/V2_9_ACCEPTANCE.md`. This milestone extends the existing native
connection pipeline rather than adding UI-only profile fields.

- add bounded keepalive and explicit reconnect behavior with observable states;
- add ProxyJump/host-chain and explicit proxy support behind transport
  abstractions;
- integrate Windows OpenSSH agent authentication and separately decide remote
  agent forwarding instead of treating the two as the same feature;
- add startup commands, environment requests, terminal type, and carefully
  bounded compatibility overrides to saved SSH profiles;
- version profile persistence and prove password, private-key, host-key, SFTP,
  telemetry, cancellation, and shutdown behavior through direct and multi-hop
  real-host fixtures.

## V2.10 — native SSH port forwarding (`0.2.10`)

Status: implemented. The frozen contract is recorded in `V2_10_SCOPE.md`, the
owned-job and resource-boundary decision in ADR 0049, and the automated,
real-host, release, and retained manual matrix in
`testing/V2_10_ACCEPTANCE.md`.

- implement local, remote, and dynamic SOCKS forwarding as owned native jobs;
- persist rules separately from credentials, expose start/stop/error state, and
  support opt-in startup with deterministic host-key and credential handling;
- bound listeners, channels, buffers, retries, and shutdown so forwarding can
  never block terminal or application teardown.

## V2.11 — terminal workspace continuity (`0.2.11`)

Status: implemented. The frozen contract is recorded in `V2_11_SCOPE.md`, the
bounded split-tree decision in ADR 0050, and the automated, runtime, release,
and retained manual matrix in `testing/V2_11_ACCEPTANCE.md`.

- add a persistent split-pane tree, keyboard resizing/focus, and session move,
  duplicate, and close operations;
- restore local terminals and SSH reconnection intents without claiming that a
  remote process survived application exit;
- retain one custom terminal item per viewport and keep all layout operations
  independent from the terminal cell model.

## V2.12 — recursive and batch SFTP (`0.2.12`)

Status: implemented; final release evidence is recorded in the V2.12
acceptance matrix.

- add multi-selection, recursive directory job graphs, aggregate progress,
  conflict policy, cancellation, recovery, and symlink boundaries;
- add richer Explorer integration, including a separately reviewed native
  virtual-file drag contract for remote entries that do not yet exist locally;
- preserve the V2.8 regular-file fast path and its byte-range resume guarantees.

## V2.13 — scripts and local notes (`0.2.13`)

Status: implemented and release-gated. The bounded data, execution, and
local-file contracts are recorded in `V2_13_SCOPE.md` and ADR 0052. Retained
human checks remain in `testing/V2_13_ACCEPTANCE.md`.

- evolve snippets into versioned multi-line scripts with typed variables,
  explicit target selection, reviewable execution, and bounded output triggers;
- add a local Markdown notes workspace with folders, search, import/export, and
  terminal-side access;
- keep arbitrary code execution, secrets, and unbounded terminal-output matching
  outside implicit automation paths.

## V2.14 — pre-V3 stable baseline (`0.2.14`)

Status: completed and release-gated as the final `0.2.x` milestone. The
implementation boundary and evidence are recorded in `V2_14_SCOPE.md`, ADR
0053, and `testing/V2_14_ACCEPTANCE.md`.

- perform long-duration, multi-session, multi-transfer, reconnect, forwarding,
  workspace-recovery, and shutdown stress passes with measured budgets;
- close accessibility, localization, mixed-DPI, installer/upgrade, migration,
  diagnostic, and error-recovery evidence across the complete supported surface;
- remove stale compatibility paths and placeholder affordances only after data
  migrations and rollback boundaries are proven;
- deliver the final Windows 11 daily-use baseline before a separately approved
  V3 direction.

## V3 AI program (`0.3.0` and later)

Status: direction selected; structured program proposed.

The owner selected native AI integration as the coherent `0.3.x` direction.
The product scope and milestone order are in `V3_AI_PROGRAM.md`, the target
design in `AI_ARCHITECTURE.md`, the security contract in
`AI_SECURITY_AND_PRIVACY.md`, the evidence matrix in
`testing/V3_AI_ACCEPTANCE.md`, and reference findings in
`research/AI_TERMINAL_LANDSCAPE.md`. ADR 0054 records the top-level semantic,
provider-independent terminal-agent decision; ADR 0055 covers shell activation;
ADR 0056 covers replay-safe execution and session ownership. The first formal
review disposition is in `reviews/V3_AI_ARCHITECTURE_REVIEW_2026-08-11.md`.

Linux portability, a standalone file-management workspace, cloud collaboration,
serial support, and remote editing remain separate future decisions and are not
implied by the AI program.

The complete V2.9–V2.14 program, invariants, and release gates remain recorded in
`PRE_V3_PROGRAM.md`. V3 still begins through separately accepted milestone
scopes; choosing the direction does not claim that `0.3.0` is implemented.
