# Terminal engine candidate assessment

Status: 2026-09-25 terminal polish and bounded protocol/performance audit validated;
remaining product gaps are listed below, not advertised as implemented.

## Outcome

The first terminal-state implementation uses `libghostty-vt` through a
ztermy-owned C++23 interface and C ABI adapter. Contour remains the fallback
C++ engine candidate. Windows Terminal is used as a correctness and
Windows-integration reference rather than imported as a terminal-state library.
The separate official ConPTY redistributable is now a pinned transport dependency
(ADR 0131); it does not replace Ghostty or the Qt Quick renderer.

The ConPTY transport, terminal state, and Qt Quick renderer remain separate
components. This lets the engine spike fail without replacing process I/O or
window code.

## Candidate comparison

| Candidate | Strengths | Main risks | Spike position |
| --- | --- | --- | --- |
| libghostty-vt | Modern VT coverage, grapheme-aware state, reflow, scrollback, C ABI, zero runtime dependencies | C API is not versioned yet; build introduces Zig | Selected for the first implementation |
| Contour vtbackend | Modern C++23, strong Unicode support, dirty state and mature terminal behavior | Large transitive CMake/vcpkg footprint; components are developed inside the full application | Fallback |
| Windows Terminal core | Excellent Windows behavior, VT parser, text buffer, MIT license | Repository is solution-oriented and tied to WIL/WinRT/DirectWrite infrastructure; no small supported CMake package | Reference |
| libvterm 0.3.x | Small MIT C library with callback API | Older VT scope; scrollback and product-level reflow remain host responsibilities; MSVC integration is not its primary distribution path | Baseline only |
| Independent engine | Full API control | Highest correctness and maintenance cost, already observed in the Rust prototype | Rejected as primary |

## Integration result

The initial build and ABI gate is complete:

1. Ghostty is pinned to revision
   `ae8727401d8c549671c36cdc326a94f47c94b635`, with the source archive hash
   checked by CMake.
2. `TerminalEngine` keeps Ghostty handles and headers out of application-facing
   code.
3. Zig 0.16.0 builds `ghostty-vt-static`; subsequent application-only
   incremental builds reuse the output.
4. MSVC dynamic Debug and static Release linkage both pass.
5. Tests cover invalid geometry, VT sequences split across writes, formatting,
   resize reflow, cell styles and colors, cursor state, and dimensions.
6. A live PowerShell session runs through independent ConPTY read/write workers
   and publishes immutable, ztermy-owned cell snapshots to one custom Qt Quick
   item.
7. An end-to-end test verifies shell startup, terminal input, parsed output,
   and prompt session shutdown.

The current Windows build disables optional SIMD dependencies. This favors a
small, deterministic first integration over maximum parser throughput. The
choice must be benchmarked again once the live session and renderer exist.

The renderer handoff gate is complete: no Ghostty handle crosses into the UI or
render thread. The first renderer uses a full-frame scene-graph texture so the
correctness boundary can be validated before glyph caching and dirty-row
batching are optimized. Unicode, IME, alternate-screen, selection, scrollback,
dirty-row, and sustained-output behavior remain part of the spike.

## Risks retained

- The upstream C API signatures are still in flux, so upgrades are deliberate,
  pinned changes.
- Zig is an additional build prerequisite and its cache path should be kept
  short on Windows.
- The initial renderer recreates a full texture for each delivered snapshot.
- Immutable snapshots now carry Ghostty's full/partial/clean damage state and
  the affected viewport rows. The adapter resets both global and row dirty
  flags only after a snapshot is copied successfully. This metadata is the
  handoff boundary for incremental renderer work; the current texture renderer
  still repaints the complete frame.
  It is a correctness baseline, not the final large-output rendering path.
- Third-party license notices must be finalized before any public binary
  distribution.

## Evidence

- Ghostty describes `libghostty-vt` as a Windows-compatible, zero-dependency
  terminal-state library with a C ABI, while noting that API signatures remain
  in flux: <https://github.com/ghostty-org/ghostty>
- The pinned Ghostty build declares Zig 0.16.0 as its minimum version:
  <https://raw.githubusercontent.com/ghostty-org/ghostty/ae8727401d8c549671c36cdc326a94f47c94b635/build.zig.zon>
- The current C API is documented in the public umbrella header:
  <https://raw.githubusercontent.com/ghostty-org/ghostty/ae8727401d8c549671c36cdc326a94f47c94b635/include/ghostty/vt.h>
- Ghostling demonstrates the C API and a renderer-independent terminal:
  <https://github.com/ghostty-org/ghostling>
- Contour is an Apache-2.0 C++23 terminal with Windows support, but its Windows
  build uses a wider vcpkg and application dependency set:
  <https://github.com/contour-terminal/contour>
- Windows documents ConPTY as UTF-8 text interleaved with VT sequences and
  requires the host to own presentation and input serialization:
  <https://learn.microsoft.com/windows/console/createpseudoconsole>

## 2026-09-25 host integration audit

This is an audit of the pinned `libghostty-vt` C ABI and ztermy's adapter, not a
claim that every sequence supported by the Ghostty application is exposed by
this pinned library. A missing host callback is not necessarily a missing VT
parser feature. Compare behavior against the pinned header and a live shell
before adding a feature or changing terminal identity claims.

Do not count ordinary modern interactions as missing merely because the host
callback table is sparse: ztermy already routes bracketed paste, focus reports
and mouse events through Ghostty, exposes OSC 8 hyperlinks, and decodes OSC 133
shell-integration marks. The table below is specifically about effects that
require the host application to answer or present something outside the VT grid.

| Host-mediated effect | Current ztermy status | Next decision |
| --- | --- | --- |
| PTY query replies | `WRITE_PTY` is registered. CPR, color-scheme reports and XTWINOPS dimensions have focused engine coverage. A local-session test reads a CPR reply inside a real ConPTY child. The loopback SSH fixture also verifies a CPR reply across the real libssh2 channel while synchronization is active. | Local Hermes classic CLI runtime evidence is recorded below; the synthetic SSH probe does not establish every remote Hermes version or configuration. |
| Color-scheme changes | `COLOR_SCHEME` reports the active terminal background as light or dark. A shell subscribed with mode 2031 receives a change report through the same PTY path. | Check a real shell that subscribes while switching application themes. |
| OSC 11 background-color query | The pinned libghostty parser replies with the configured background RGB through `WRITE_PTY`; a focused engine check confirms a light theme returns white. | Keep this separate from the newer light/dark mode query; Shells may use either. |
| Device attributes and XTVERSION | Pinned libghostty supplies usable default replies without custom callbacks; a diagnostic test confirmed both. | Do not register callbacks merely to duplicate defaults. |
| OSC 52 clipboard write | Bounded text/UTF-8 callback delivery is implemented for local and SSH sessions; clipboard read is not advertised. The documented 0.4.3 behavior deliberately has no modal prompt, so there is no per-host permission boundary today. | Preserve the size/lifetime limits. Any new remote-host consent policy would be a product change, not a claim about the current implementation. |
| OSC title, working directory, BEL, ENQ, desktop notification and progress | Title and working-directory metadata are read from retained terminal state. Progress and notification callbacks are now registered with bounded retained status; BEL and ENQ callbacks remain unregistered. | OSC titles do not invoke manual renaming. OSC 9;4 drives tab progress; OSC 9/777 drive rate-limited plain-text in-app notifications. This is not Windows taskbar progress or a system toast integration. |
| DEC synchronized output (`CSI ? 2026 h/l`) | Local/SSH snapshot suppression and a one-second fallback are implemented. Raw local-session and real loopback SSH checks cover held frames, queries, timeout recovery, host interaction and final output. | Installed ConPTY can change the application's interval; retain the explicit platform-test skip and do not promise preservation across every Windows version. |
| Kitty inline images | The pinned C API exposes image storage and placement data, but ztermy does not configure a nonzero Kitty image storage limit or a PNG decoder, and its snapshots and Qt renderer have no image-placement representation. This is not a working image feature even if the parser recognizes the sequence. | Treat as a separate opt-in design with strict byte/pixel limits, decode off the GUI thread, GPU/CPU cache budgets and eviction, and security review. Do not advertise image support based on parser coverage alone. |

DEC 2026 split-write validation and its bounded host-side remedy are complete
for the recorded raw-session and loopback-SSH scenarios. The missing image path is substantially larger and
has no reported user workflow yet. OSC titles need a manual-name precedence
rule before implementation. None of these should displace a reproducible
rendering defect on the affected machine or justify a full test sweep by
themselves.

The local-shell catalog is a separate host concern: it caches detected Shell
executables and can be refreshed from Settings. Newly created ConPTY children
now receive a fresh registry-backed PATH while retaining transient process
variables, except for stale parent-terminal identity markers. The child uses
`TERM=xterm-256color` and `COLORTERM=truecolor`; see ADR 0127. A new tab
therefore sees newly added commands in an already known
Shell; discovering a newly installed *Shell executable* during the same app run
still depends on catalog refresh and where that executable is installed.
An interactive dynamic Release check also launched ztermy with
`ZTERMY_STALE_PATH_SENTINEL` prepended only to the application's process PATH;
a newly opened local PowerShell returned `False` for
`$env:Path.Contains('ZTERMY_STALE_PATH_SENTINEL')`. This validates the real
application-to-ConPTY handoff without editing the registry. The focused
ConPTY test separately checks that a nonempty registry-backed PATH and a
transient non-PATH variable both reach the child.
SSH profiles already default their PTY type to `xterm-256color`, so the local
change does not create a terminal-type mismatch. Remote environment variables
are profile-scoped SSH `setenv` requests; ztermy does not silently send the
local `COLORTERM`, because a server rejecting an environment request currently
fails channel opening. Remote true-color negotiation needs a separate,
server-tolerant decision rather than copying the local ConPTY policy.

The installed Hermes classic CLI uses `prompt_toolkit` 3.0.52 for its CPR
warning. Its renderer asks for CPR when the cursor position is unknown, waits
two seconds, and only then prints the warning if no response was accepted.
Therefore the warning can mean a lost/late reply in a particular session, not
that all ztermy terminals lack CPR. The local ConPTY end-to-end probe above
completed in under one second. The installed Hermes Agent v0.21.4 classic CLI
was subsequently launched in the rebuilt dynamic Release through local
PowerShell with a separate `HERMES_HOME` and `--safe-mode --cli`. It reached
the interactive prompt and first-run provider question without a CPR warning,
including after the two-second timeout. No provider was configured and no
query was sent. This closes the local reproduction for that version and shell,
but not a different Hermes version, the original affected session, or an
SSH-hosted CLI.

The current performance program has already rejected a backing-store rewrite
for the measured desktop burst: exact row reuse was low, text paint dominated,
and idle cursor texture uploads were removed by the separate cursor node. A
single later stress run is diagnostic, not evidence of a regression or memory
leak. Keep any renderer replacement behind a repeated, like-for-like profile
on the affected workload and hardware.

On 2026-09-25, five consecutive isolated runs of the current dirty-worktree
dynamic Release (`ztermy.exe` SHA-256
`24971C8B5DA6281B8E725D97067B07E8AFFEF05D581A5B3D388A2738A6EFC73D`) used
the built-in 20,000-line benchmark, dark terminal theme, true opaque D3D11
surface, DPR 1, and separate `performance-opaque/run-925002` through
`run-925006` data directories. All completed and passed responsiveness checks.
Completion was 1,529–1,670 ms (median 1,633 ms); maximum GUI heartbeat gap
was 21–35 ms (median 24 ms); paint p95 histogram bounds were 4–8 ms (median
8 ms), and idle terminal texture upload was zero bytes in every run. One
separate opt-in phase diagnostic (`performance-opaque-paint-phases/run-925101`)
put text paint at p95 <= 8 ms, versus background paint <= 250 µs; the
diagnostic itself adds measurement work and is not a sixth baseline sample.
An older single run had a 2 ms paint-p95 bound but rendered more frames; it is
not a matched A/B baseline and cannot establish a regression or improvement.

After the cell-style and local environment changes, five more isolated runs of
the same 20,000-line opaque D3D11 scenario used `performance-opaque/run-925201`
through `run-925205` (`ztermy.exe` SHA-256
`385B9350F0C6B8ACFFE18DDBD8175E90D8F59111D79C4EF61944A3E1B6C3E0E8`).
Every report matched the earlier environment fields, completed, and reported
zero idle texture upload. Completion was 1,528–1,672 ms (median 1,654 ms),
maximum GUI heartbeat gap 21–23 ms (median 22 ms), and paint-p95 histogram
bound 8 ms in all five runs. The earlier medians were 1,633 ms, 24 ms, and
8 ms respectively. A 21 ms completion-median difference across these small,
non-interleaved samples is not evidence of a meaningful regression or gain;
the ordinary-output scenario also does not measure dense colored underlines.

After the light-surface contrast adjustment, one further matched dark/opaque
run (`performance-opaque/run-925301`) completed 20,000 lines in 1,646 ms,
reported a paint-p95 bound of 8 ms, a maximum GUI heartbeat gap of 23 ms, and
zero idle texture upload. Its environment fields match the earlier runs. This
single diagnostic rules out an obvious gross regression in the dark/default
background path, but does not measure the light-theme or many-explicit-background
paths and is not a new statistical comparison.

For the remaining report of character drift *without* ligatures, a Qt font
measurement on this machine at 14 px found the same 7.98438 px advance for
`M`, space and `i` in regular/bold Cascadia Mono and Consolas. The renderer
positions non-ligature cells on that same grid. This rules out a simple
`M`-versus-space width mismatch for these fonts here; it does not rule out
fallback glyphs, a different font or DPI on the affected machine, or a
shell-specific rendering path. Do not change global cell padding based on
this measurement alone.

For a machine where logs cannot be exported, use a visual-only controlled
reproduction before changing renderer geometry: record the Windows scale,
terminal font/size and ligature setting; in PowerShell print
`'0123456789MMMMiiii    X'`; photograph the same line unselected, with only
`MMMM` selected, and after switching focus away and back. If the `X` grid
position moves, investigate shaping or snapshot geometry; if only a glyph
intrudes into adjacent whitespace, investigate font fallback and glyph
overhang. Repeat with Consolas versus Cascadia Mono only if both are already
available. This distinguishes two causes without collecting terminal input,
private session data or diagnostic logs from the restricted machine.

The pinned Ghostty style already exposes SGR 2 `faint`, but ztermy previously
dropped that flag when copying cells into its snapshot. Thus a shell prediction
that used faint instead of an explicit pale RGB could look identical to typed
input. The snapshot and renderer now retain the flag and fade the foreground
toward the cell background; on light themes the result keeps a 4.5:1 minimum
contrast so the suggestion remains readable. Focused engine and Qt Quick pixel
checks cover this distinct failure path. This does not imply that every shell
uses SGR 2 for its suggestions: some emit their own colors, which are handled
by the separate light-theme contrast guard.

An isolated dynamic Release window using the `ztermy-light` theme reproduced
the original PowerShell inline-prediction case on 2026-09-25: after typing
`Get`, PSReadLine drew the remainder of a history command in pale gray. The
first contrast guard only covered cells without an explicit background, and
its 4.5:1 nominal target left too little visual headroom at terminal text size.
The renderer now evaluates the effective background of each cell (including
explicit and highlight backgrounds), targets 5.5:1 for low-contrast foregrounds
on light surfaces, and retains at least 4.5:1 for SGR 2 faint text. A targeted
Qt Quick pixel test first failed on all four default/explicit and
opaque/transparent light-background combinations, then passed after the fix.
The rebuilt application's real `Get` prediction is visibly distinct from the
typed input. This is local runtime evidence, not a claim about every light
theme, font, screen or Shell; the affected machine should still be rechecked.

The same cell-style audit found that ztermy previously collapsed every SGR 4
underline into a plain line and discarded SGR 58 underline colors. The snapshot
now preserves single, double, curly, dotted and dashed styles and resolves a
separate RGB or palette underline color against Ghostty's *current* palette,
including OSC palette overrides. VT-fed engine checks verify both theme and
OSC 4 palette changes recolor an existing underline; a Qt Quick pixel check
covers colored curly and double underlines, including direct runs at 125%,
150% and 200% Windows scale factors. Ghostty also exposes the SGR 5
text-blink flag, which ztermy still does not paint; this remains a known visual
gap, not a claim that the parser lacks SGR 5. Unlike cursor blinking, text blink
would add periodic row invalidation, so prioritize it from a concrete workload
and accessibility behavior rather than enabling an idle animation by default.

A separate, reproducible DPI issue did affect the optional block cursor over
ligatures: its own texture was rasterized at a cell-local phase and linearly
resampled at fractional scale factors. Aligning that texture to physical pixel
boundaries and rendering the complete ligature run in the aligned cell leaves
only boundary-pixel coverage differences in focused Qt Quick captures at 100%,
125%, 150% and 200% scale. This does not prove the unrelated no-ligature drift
reported on another machine is fixed.

The DEC 2026 investigation on 2026-09-25 exposed a test precondition failure.
Direct engine input sets and clears the synchronized-output mode correctly,
and taking a snapshot does not reset it. However, with system `conhost.exe`
10.0.26100.8875, a PowerShell fixture's received output contained a synchronization
end marker **before** its final text, despite the script placing that marker
after the final text. A startup `2026h` therefore does not prove that ConPTY
preserved the application's interval. Adding a separate startup write and
reducing the inter-write delay from 350 ms to 50 ms still exposed an intermediate
frame. Microsoft's [implementation discussion](https://github.com/microsoft/terminal/pull/18826)
mentions a 100 ms timeout, but the 50 ms result means timeout alone is not an
established explanation here. The local integration test now explicitly skips
when the received interval does not surround the fixture's text; this is missing
coverage, not a passing frame-suppression result. This prompted the raw-session
and loopback SSH checks recorded below. No arbitrary render delay should be added
to conceal a synchronization interval that the transport has already ended.

Subsequent raw local-session boundary checks bypassed ConPTY while retaining
the production output consumer, command worker, snapshot delivery and Qt timers.
They verified suppression until the end marker and fallback without additional
output. They also first failed, then passed, for two bugs in the initial change:
after a timeout the engine mode was not reset, so the next update leaked an
intermediate frame; and selection was held behind application synchronization.
Host view changes now cancel the hold, and timeout resets both engine mode and
deadline. The one-shot timer uses `Qt::PreciseTimer` to prevent an early callback
from failing the deadline check without scheduling another wakeup. CPR is also
verified directly while synchronization is active. A separate SSH completion
test first lost the final frame, then passed after transferring that frame into
the owner-thread disconnect event. The related local/engine/SSH checks pass.
The later loopback SSH and application checks below supply transport, resize
and window runtime evidence. These focused tests are not a full regression
run. Presentation policy is recorded in ADR 0128.

The five file-size regressions found by the code-health gate were resolved
without raising its baseline: local/SSH snapshot publication now lives in
`LocalTerminalPresentation.cpp` and `SshTerminalPresentation.cpp`; platform-neutral
input types and Ghostty mappings live in `TerminalInput.h` and
`GhosttyInputMapping.cpp`; the Qt scene-graph painter lives in
`TerminalItemPainting.cpp`, sharing layout constants with the input item through
`TerminalLayoutMetrics.h`. The structural gate passes after this extraction.
The dynamic Release application and affected test targets build successfully.
Focused input-mapping, engine-style and session-publication checks pass. Actual
Qt Quick drawing checks also pass for all four light prediction-background
variants, colored underlines, selection/ligature geometry, wide cells/cursor
pixels and cursor-only texture reuse. This validates the moved painter at the
item boundary.

The optional `synchronizesOutputOverLoopbackSsh` test then passed across a real
libssh2 connection to a one-connection, loopback-only Paramiko fixture. It verifies
CPR replies while a frame is held, no intermediate frame before the end marker,
fallback with no further output, suppression in the next transaction, immediate
90-column resize and final `BYE` output on disconnect. Fixture commands travel
over the test process's stdin, not terminal input, so they cannot accidentally
cancel synchronization through the host-interaction path. The fixture exposes
no OS shell, filesystem or forwarding, and its in-memory host key is disposable.
For reproduction, install Paramiko in a dedicated Python environment, set
`ZTERMY_TEST_SSH_FIXTURE_PYTHON` to that interpreter, and run that single Qt test.
This run used Paramiko 5.0.0 installed only under `build/test-tools/ssh-fixture`
with that directory set as `PYTHONPATH`; it adds no shipping dependency.

The rebuilt application also passed its isolated opaque/D3D11 terminal benchmark
in `build/runtime-checks/terminal-final-20260925`: 20,000 lines completed in
1,584 ms, maximum GUI heartbeat gap 19 ms, paint-p95 histogram bound 2 ms,
successful resize/scrollbar checks and zero idle terminal texture upload. The
saved `terminal-render-complete.png` was visually inspected: final output and
PowerShell prompt use a consistent grid. This single run is a post-change
runtime check, not a statistically established performance improvement. The
application exited with code 0 and the test server was confirmed gone.

## Polish acceptance and remaining gaps

The requested light-theme/prediction readability and fresh-PATH behavior are
implemented and covered by focused tests plus isolated application evidence.
The final fresh-PATH ConPTY check also passes. SGR faint/underline preservation,
fractional-DPI cursor geometry and synchronized-output presentation have the
specific regression evidence recorded above. Dynamic Release builds; formatting,
diff checks and the unchanged code-health gate pass. Only related tests were
run, not a full-suite regression. These checks were completed before commit.

The protocol/interaction/performance audit is complete for the pinned adapter
and measured scenarios, with explicit limitations rather than a claim of parity
with every mainstream terminal. Suggested follow-up order:

1. Closed by owner confirmation on 2026-09-26: the reported no-ligature drift
   is fixed in current use. The earlier visual-only procedure remains historical
   diagnostic guidance, not an outstanding acceptance requirement.
2. Approved on 2026-09-26: transient OSC titles and shell progress/notifications.
   Terminal-controlled titles have a setting; manual renaming pins the title
   regardless of that setting, until explicitly cleared by the user. The pin
   must survive layout restoration. The engine snapshot now carries a bounded
   transient OSC 0/2 title, including title-only output and explicit clearing.
   Display precedence, the setting and schema-9 manual-name persistence are now
   implemented in the worktree with focused controller and real local Shell
   verification (see below). Progress and in-app notifications are now connected;
   system taskbar/toast presentation and detached-window routing are not yet
   covered by the current main-window implementation.
3. Approved on 2026-09-26: both Kitty and Sixel inline images, plus SGR text
   blink, with memory/accessibility budgets. These are not yet shipped
   capabilities; approval must not be mistaken for implementation evidence.

No engine replacement or broad rendering rewrite is justified by the measured
results. The actionable failures in this work were host integration and
presentation behavior, which remain under ztermy's control with the current
Ghostty adapter.

Primary protocol references: the pinned
[`GhosttyTerminalOption` header](https://github.com/ghostty-org/ghostty/blob/ae8727401d8c549671c36cdc326a94f47c94b635/include/ghostty/vt/terminal.h)
defines the callback boundary; Ghostty's
[VT reference](https://ghostty.org/docs/vt/reference) describes the upstream
sequence surface, which may evolve independently of this pin.

## Bulk tab close: 2026-09-26

`closeOtherTerminalTabs` and `closeTerminalTabsToRight` previously called the
single-tab close operation repeatedly. Each iteration changed active context,
published the tab list, and saved the workspace. Closing sessions was already
asynchronous; moving that stop operation to another thread would not remove
the measured UI notification cost.

The close operation now detaches all selected workspaces before publishing one
final active context/tab list and persisting once. Recent-close descriptions
retain their previous order; tabs belonging to other windows remain untouched.
The focused regression observes the published state, not just final tab count,
so an intermediate activation/rebuild is detectable.

The lifecycle harness now waits for each new tab's viewport-backed startup
before selecting another tab. Previously, its concurrent stage could leave
the first two tabs pending and fail without exercising concurrent teardown.
The harness lives in `src/ui/TerminalLifecycleRuntimeSmoke.h`.

Dynamic Release comparison, same binary and isolated fresh data directories:
each run opens/closes eight sequential sessions, then starts eight concurrent
local sessions and closes seven. `ZTERMY_TEST_SEQUENTIAL_TAB_CLOSE=1` composes
the public single-tab API in the old reverse order; the default exercises the
batch API. `ZTERMY_TAB_TIMING=1` is enabled in both arms.

| Run order | Close method | Synchronous close time | Shutdown |
|---|---|---:|---:|
| 1 | Individual | 1068 ms | 164 ms |
| 2 | Batch | 133 ms | 239 ms |
| 3 | Batch | 124 ms | 164 ms |
| 4 | Individual | 1082 ms | 166 ms |

All four lifecycle runs exited 0. Logs are in
`build/runtime-checks/close-{sequential,batch}{,-repeat}-20260926/logs/`.
Five related controller cases pass, including window scope, reopening,
detached-close selection and moved-session ID resolution. Test PIDs and direct
children were confirmed gone. These are small-sample local measurements of
synchronous API duration, not a frame-latency distribution or proof that every
large-Pane teardown stall is resolved. Background compilation overlapped part
of the second batch run, so these numbers are supporting evidence rather than
a controlled performance benchmark suitable for a universal speedup claim.

Dynamic Release builds, targeted clang-tidy on the three changed translation
units (warnings as errors), formatting, diff checks, and the unchanged source
size/dependency gate pass. No full-suite regression was run for this iteration.

## Large-pane close and action presentation: 2026-09-26

The opt-in lifecycle path `ZTERMY_TEST_PANE_CLOSE=1` builds four/eight running
local panes, then measures closing one active pane and closing the workspace.
A 5 ms GUI heartbeat covers the call and another 500 ms of event processing,
so a queued stall is not hidden by measuring only the synchronous return.
`ZTERMY_TAB_TIMING=1` separates state/retirement, active-context notifications,
and finished-session destruction; it remains off during normal use.

The initial eight-pane single close took 258/264 ms: state update and session
retirement took 6–9 ms, while UI notification took 249/258 ms. Finished-session
destruction was below the millisecond timer resolution. Further timing isolated
the expensive notifications to terminal-list and active-tab changes rather
than SFTP, AI, or viewport delivery.

Those notifications invalidate shortcut labels. QML indexed `controller.actions`
repeatedly, and every property read rebuilt all action QVariant maps and their
translated labels. Merely assigning that reference to a local QML variable
left eight-pane single-close time at 238 ms. The implemented fix caches the two
action presentations (terminal available/unavailable) in `ActionRegistry` and
invalidates them on accepted shortcut edits, reset/import, and UI retranslation.
QML shortcut lookup now uses one `find` expression rather than repeatedly
fetching the property. Returned lists retain Qt's copy-on-write value semantics.

| Scenario | Baseline close ms | Cached close ms, two runs | Cached max GUI gap ms, two runs |
|---|---:|---:|---:|
| 4 panes, close one | 185 / 185 | 28 / 26 | 28 / 26 |
| 4 panes, close workspace | 204 / 210 | 50 / 52 | 58 / 60 |
| 8 panes, close one | 258 / 264 | 30 / 32 | 30 / 33 |
| 8 panes, close workspace | 282 / 302 | 62 / 58 | 75 / 75 |

Evidence directories: `build/runtime-checks/pane-close-{baseline,context,snapshot,
cache,cache-repeat}-20260926`. All runs exited 0. The setup uses idle local shells
and nested splits at 1120×800; it does not establish equivalent latency for
huge scrollback, a different GPU/DPI configuration, or SSH teardown. Residual
workspace-close cost remains measurable; no claim of universally frame-perfect
teardown is made.

Focused tests cover cached shortcut updates/unbinding/reset/import, context
availability, retained snapshots, and controller retranslation after an already
read action list. The existing shortcut dispatch and adjacent bulk/window-scope
tests pass. Dynamic Release, QML formatting/lint, targeted clang-tidy (warnings
as errors), source-size/dependency and diff checks pass. Test processes and
direct children were confirmed gone. No full suite was run for this iteration.

## Title-policy persistence groundwork: 2026-09-26

Application settings schema 39 adds `allowTerminalTitleChanges` (default true).
Schema 38 and earlier acquire that default; schema 39 requires an actual JSON
boolean, not a coerced string/number or a missing field. The fixed schema-38
fixture in `tests/fixtures/settings/schema-38.json` includes non-default theme,
session restoration, shortcuts and provider settings. Migration verifies that
every existing JSON field survives unchanged, then that disabling the new
preference persists. Separate malformed-input checks use fresh stores so backup
recovery cannot mask rejection. The application-settings owning suite, dynamic
Release test build, targeted clang-tidy, format and code-health gates pass.

The preference is now exposed in Window behavior. Snapshot titles are projected
per pane, with workspace/manual names taking precedence. An empty manual rename
clears the pin; an empty program title returns to the Shell/Profile default.
Workspace schema 9 preserves manual names separately from default labels;
older schemas retain existing names as fixed names because their origin was
not recorded. See ADR 0129 for ownership and migration semantics.

Controller checks cover preference toggles, program changes while pinned,
clearing, restoration, active-pane switching, duplicate/reopen and transient
titles staying out of persistence. The real local PowerShell/ConPTY smoke at
`build/runtime-checks/title-policy-20260926` exited 0 and reported
`programTitleAndManualOverride=true`; PID 25732 and its direct children were
confirmed gone. This is local runtime evidence, not a live SSH-server check
or completion evidence for the remaining window/image/notification work.

## Program progress and in-app notifications: 2026-09-26

The pinned Ghostty callbacks now retain OSC 9;4 progress (remove, determinate,
error, indeterminate, paused) and OSC 9/777 notifications. No parallel escape
sequence parser or cell-level QML objects were introduced. Local and SSH
snapshots carry the same state; title/progress UI notifications are coalesced.
Only the active pane's progress is currently projected onto its workspace tab.
Ended sessions hide progress even if the program did not send a remove report.

Notifications accept at most 512 title bytes and 4096 body bytes, require valid
UTF-8, and retain one shared immutable accepted message per engine. Bursts are
dropped with a two-second per-engine cooldown and a second two-second global
presentation cooldown. Allocation failure preserves the prior message and
backs off rather than unwinding through the C callback. Snapshot coalescing
does not lose the retained message; ordinary redraws cannot replay it.

Main-window notifications use the existing non-modal action toast, plain text,
and an application-owned source heading. They do not request activation or
keyboard focus. The displayed message is capped at 1024 UTF-16 units and is
not persisted. This deliberately does not claim Windows system toast, taskbar
progress or independent detached-window delivery support; those presentation
paths still need integration/acceptance alongside the remaining window work.
The tab indicator respects effects reduction/disablement and does not animate
when its window is hidden or minimized.

VT-fed tests cover fragmented progress termination, state transitions, removal,
retained snapshot independence, both notification protocols, oversized payloads
and flooding. Controller tests cover frame replay suppression, progress-only
updates and clearing the visible indicator when the session ends. Dynamic
Release and QML checks pass. The real PowerShell/ConPTY run at
`build/runtime-checks/terminal-status-repeat-20260926` reports
`progressAndToast=true`, `focusPreserved=true` and exit 0. PID 3308 and direct
children were confirmed gone. The first run failed because QObject-only lookup
missed Repeater delegates; the corrected check uses the existing visual-tree
lookup and observes the actual tab property and open toast, not just C++ state.

## SGR text blink: 2026-09-26

The pinned Ghostty parser already accepts SGR 5 and 6 as the same blink
attribute, with SGR 25 and 0 clearing it. The snapshot conversion previously
dropped that attribute. `TerminalCell::blink` now retains it independently of
invisible text and cursor blink. VT-fed regression checks cover fragmented
input, both enable codes, both reset codes, unchanged text/cell width/cursor
advance, overwriting a previously blinking cell, and immutable old snapshots.
These checks plus the adjacent styled-cell and render-damage checks pass in
dynamic Release.

The viewport caches unselected blinking ink in a separate transparent texture,
bounded by the ink's viewport-clipped rectangle. Backgrounds and selected text
stay in the base image. Mixed-selection ligatures are shaped as complete runs
and their colored pixel segments are assigned to the two layers; phase changes
never split or reshape runs. The 530 ms phase timer changes only scene-graph
visibility in the ordinary no-IME path, with no texture upload. During IME
composition the existing full composition redraw remains in use. Removing all
blinking ink removes the extra texture on the next full paint.

Text blink is independent of cursor blink/focus. Reduced or disabled effects,
performance mode and the system animation preference keep text continuously
visible via the shared Motion policy. Hidden/minimized windows and hidden
viewports stop the text timer; a viewport without unselected blinking ink does
not start it. Both SGR blink speeds intentionally use the same gentle cadence,
matching the pinned parser's single attribute.

Real Qt Quick window captures verify changing only unselected blink pixels,
stable selected/ordinary text, restoring visible ink when disabled, and zero
uploaded bytes over phase/policy changes. The check passes at normal and 125%
scale, alongside the existing ligature-selection geometry check. No full test
matrix or release packaging was run for this change.

The stronger check that removes blink attributes exposed LCD/gamma edge-color
differences between opaque and transparent text. The final renderer uses one
reusable transparent row buffer plus transparent cursor ink, rather than a
second viewport-sized image. All buffers retain native premultiplied ARGB;
RGBA8888 was evaluated and rejected after CPU measurements. See the pinned
[Qt 6.8.3 raster implementation](https://raw.githubusercontent.com/qt/qtbase/v6.8.3/src/gui/painting/qpaintengine_raster.cpp)
for the format-dependent glyph-cache paths. Empty rows skip ink compositing.
Layer bounds align to physical pixels at fractional DPI. Strict pixel equality
now also covers returning from blinking to ordinary text, both with and without
ligatures. Blink runs are rasterized at the original fractional baseline and
placed at physical-pixel-aligned origins, rather than replaying device-dependent
text commands into the cached layer. Blink flags, like selection, do not split
shaping runs. The last segment preserves the rasterizer's guard pixels instead
of trimming filtered glyph coverage at the advance boundary.

One hardware capture run stalled in `QQuickWindow::grabWindow`, with the render
thread waiting inside `QRhi::beginFrame`. The owned process (PID 34620) was
terminated after a non-invasive stack capture. A D3D software-adapter isolation
run then exposed the reproducible pixel mismatch above; it was not treated as
a pass or masked by a looser assertion. The post-update capture now waits for
`frameSwapped` before comparing the result. A second default-hardware capture
attempt (PID 23024) also stalled and was cleaned up. The new blink test now uses
asynchronous `grabToImage` with a bounded wait rather than blocking swapchain
readback. Its three data cases (unligated cells, mixed selection and mixed blink
without selection) pass on the default hardware adapter at normal and 125%
scale. Adjacent selection, wide-glyph/cursor, light-theme prediction, underline,
cursor-cache and IME pixel checks pass with the software-adapter preference.
This fixes the test capture path; it is not a general Qt/driver hang fix.

### Full-paint cost comparison

`ZTERMY_TEST_PAINT_PROFILE=1` enables `reportsOptInFullPaintProfile` in the
terminal-item test binary. The fixed workload is 128 columns by 48 rows in a
1400x900 viewport at DPR 1, with either 3 or 48 populated rows. Each scenario
discards 8 warm-up frames and records 64 isolated full paints on the render
thread, not screenshot/wait or build time. Same dynamic Release configuration
and hardware adapter; baseline uses only the painting source from `0a1a7d8`,
with the identical current harness and remaining code. The candidate source
was restored immediately after the baseline run.

| Workload | Baseline median / p95 (µs) | Final median / p95 (µs) |
|---|---:|---:|
| Sparse, no ligatures | 1089 / 1145 | 1132 / 1305 |
| Dense, no ligatures | 13741 / 14217 | 13421 / 13938 |
| Sparse, ligatures | 573 / 682 | 570 / 742 |
| Dense, ligatures | 4812 / 4974 | 4649 / 5027 |

The first RGBA-ink candidate cost 8184 µs median for dense ligated text and was
not accepted. Native ARGB plus skipping empty-row composites brings this
synthetic workload near its baseline; these samples do not establish a general
speedup, nor measure SSH throughput or every font/DPI. The separate phase test
continues to require zero texture-upload bytes while blinking/turning motion
off. The opt-in profile has no machine-dependent timing pass threshold.

## Inline images: implementation in progress (2026-09-26)

The pinned Ghostty C API already owns Kitty transmissions, image generations,
placements, source crops, and viewport-relative positioning. Reuse that state
instead of parsing Kitty a second time. The domain snapshot now carries owned,
immutable pixels plus placement metadata; it never exposes borrowed Ghostty
pointers to the GUI/render thread. Equal generations reuse the same pixel
object, including multiple placements. Cache references are weak so deletion
releases pixels after the last displayed/queued snapshot releases them.

The bridge bounds a snapshot to 32 MiB of unique image pixels, 8192 pixels per
dimension and 4096 visible placements. These are **snapshot bounds, not yet a
complete process-wide memory policy**. The pinned library's original terminal defaults
use 10,000,000 bytes of image storage per screen (the standalone Ghostty
application uses 320,000,000), while the loader independently allows up to
400 MiB. A fail-closed build patch now reduces that loader/inflater payload
ceiling to 32 MiB and dimensions to 8192. Runtime initialization explicitly sets
32-MiB storage per screen and 16-KiB APC buffers, and disables file, temporary-file
and shared-memory media. Direct chunked terminal-stream transmission is sufficient
for remote sessions. See [ADR 0130](../adr/0130-terminal-inline-image-resource-policy.md).

The valid over-budget zlib test initially found no rejection reply: upstream
lost the first chunk's image ID on final-chunk failure. The patch now retains
that response identity and frees a pending loader immediately on append failure.
Focused dynamic Release checks pass for a valid 4096-by-2049 RGBA zlib stream
rejected specifically by inflation (not a later storage ENOMEM), an unfinished
raw multipart upload crossing 32 MiB, and successful image/text output afterward.
Those two checks plus five existing Sixel/Kitty/PNG cases pass (866 ms total).
Local FetchContent source overrides also apply the patch before entering the
dependency CMake directory; its archive rebuild depends on the patched Zig files.

Focused dynamic Release tests feed real, fragmented Kitty sequences and check
same-ID/same-size pixel replacement, old-snapshot immutability, same-generation
pixel sharing, deletion lifetime, alternate-screen isolation, and scroll
position changes. These pass alongside blink attributes, split VT input,
screen switching and render-damage regressions (6 cases plus init/cleanup).
No full regression was run for this intermediate domain-layer change.

An additional image-only deletion check initially failed: Ghostty's text damage
remained clean after deleting a placement. The bridge now compares graphics
storage generations independently and forces a full viewport repaint when the
image state changes. The failing deletion assertion now passes; this prevents
the future compositor from leaving a stale deleted image on screen.

The viewport now composites ordinary placements in the three protocol layers:
below explicit cell backgrounds, below text, and above text. Source cropping,
pixel offsets and clipping use the snapshot geometry. Above-text imagery uses
a bounded-to-visible-area texture above blinking ink and below the cursor;
phase-only blink paints retain that texture and upload zero additional bytes.
Images bypass text contrast/faint-color adjustments and pane background opacity.

Dynamic Release hardware Qt window captures pass at default and 125% scaling:
explicit background coverage, text/image order, cropped source scaling,
padding clipping, overlay deletion, and a blinking glyph hidden by an opaque
image. A first whole-frame assertion exposed one glyph overhang pixel outside
the fixture's image; the fixture was enlarged to cover the glyph, without
weakening the pixel equality assertion. The three existing blink cases also
pass. A separate integration case sends a real RGB Kitty transmission through
Ghostty, displays its snapshot in a real terminal item, checks the red pixels
at the requested cell and verifies deletion removes them. These are test-window
captures, not yet an acceptance run of a shell image tool in the full app.

PNG decoding is now registered once before application terminal workers start,
via a separate Qt-backed infrastructure target; the domain engine stays Qt-free.
The callback returns allocator-owned, straight RGBA bytes using Ghostty's own
allocator, rather than crossing the MSVC/Zig heap boundary. It accepts at most
8 MiB of encoded PNG, 8192 pixels per dimension, and 32 MiB of decoded raster
(preflight budgets eight bytes per pixel for 16-bit PNG). It processes only
critical pixel chunks and transparency, rejects unknown critical chunks, and
does not inflate textual/ICC metadata or run animation. This deliberately
omits embedded color-profile processing; the output is static raw color data.
The format constraints follow the
[PNG chunk specification](https://www.w3.org/TR/png-3/#5Chunk-layout).

Real fragmented Kitty `f=100` transmission preserves RGBA values including
half-transparent pixels. Focused tests also reject valid over-wide and
over-raster-budget PNGs, huge declared dimensions with repaired IHDR CRCs,
overflow-sized chunks and truncation;
valid compressed text metadata does not change decoded pixels. Those two cases
and the two existing Kitty lifecycle cases pass in dynamic Release. The valid
oversize fixtures are important: a mismatched IHDR/IDAT image could fail in the
codec even without our admission check and would not prove the limit works.
The complete dynamic Release application also builds with startup registration.
These decoder bounds complement the separately patched Ghostty multipart/zlib
loader limit. They do not bound total memory retained by multiple sessions.

The Qt-free Sixel payload decoder now incrementally accepts raster attributes,
RGB/DEC-HLS color definitions, repeat runs, carriage return, next-band controls,
and transparent/opaque backgrounds. It retains color-register indexes until
final RGBA conversion and returns the resulting palette, aspect ratio and sixel
cursor coordinates for the eventual terminal adapter. The DEC defaults and
blue-at-zero HLS convention come from the original
[color mapping chapter](https://vt100.net/docs/vt3xx-gp/chapter2.html) and
[Sixel chapter](https://vt100.net/docs/vt3xx-gp/chapter14.html), not another
terminal's implementation.

Limits are 8 MiB input, 8192 pixels per dimension, 8,388,608 pixels
(32 MiB RGBA), and separate 64-million-unit budgets for painting and storage
growth. Rejected streams release scratch storage immediately. The decoder does
not preallocate a full-size canvas and does not expand RLE into a temporary
input string. Focused tests cover every chunk size of a multi-color payload,
transparent overprinting, DEC hue zero, raster dimensions as minimum extent
rather than crop limits, incomplete/overflowed repeat counts, and repetitive
overdrawing of a small final bitmap. These three tests plus adjacent PNG and
Kitty lifecycle tests pass in dynamic Release (5 cases plus init/cleanup).
The accompanying graphics-stream framer recognizes seven-bit and raw C1 Sixel
DCS boundaries, distinguishes UTF-8 continuation bytes from C1 controls, and
passes other OSC/APC/DCS traffic through unchanged. Its retained header is
bounded to 128 bytes; payloads are delivered as borrowed spans instead of
accumulating a second image buffer. Ordinary 64 KiB text is forwarded with one
sink call. CAN/SUB, an interrupted escape, and stream EOF cancel unfinished
images. All fragment sizes of mixed VT/Kitty/UTF-8/Sixel input, both ST forms,
overlong headers, cancellation and EOF recovery pass focused tests (together
with Sixel decoding and ordinary VT, 6 cases plus init/cleanup).

The framer is now connected to `GhosttyTerminalEngine::feed`. Sixel uses
internal image IDs and pinned placements through a small ztermy-owned Zig/C
insertion extension, without entering the Kitty upload parser. The earlier
base64 adapter was removed after a real mixed-upload test showed that it
corrupted an unfinished Kitty transfer. Pixel aspect is expanded row by row
into the storage allocation, with the same 8192-dimension/32-MiB raster ceiling.
Index controls advance the text cursor without changing its column or replacing
the application's saved cursor. Mode 8452 instead ends at the right of the last
image row. Fragmented real-protocol tests cover bottom-edge scrolling, saved
cursor restoration, alternate screens, no spurious PTY replies, and a larger
RGBA raster with 2:1 aspect and transparent rows.

Sixel is now advertised in primary device-attribute replies. XTSMGRAPHICS reads
report 256 color registers and current/maximum pixel geometry; writes to these
policy attributes fail explicitly, as do requests for unsupported ReGIS. A
fragmented query test checks exact single responses and an unchanged cursor.
DECSDM absolute-screen
mode now inserts at the active-screen origin and clips to its pixel extent,
without moving the text cursor or touching its pending wrap. The mixed-upload
and absolute-mode tests first failed on the base64 adapter and now pass, alongside
four aspect/scroll/resource regressions. Real Qt hardware-window pixel captures
also pass for both protocols, including deletion and 125% scaling. These are
test windows, not full-app shell-tool acceptance.

The new `--terminal-image-smoke` launches an isolated local shell, writes known
Sixel and Kitty images through ConPTY, captures the viewport asynchronously,
and shuts the session down. Its first run stopped at hidden-startup window
exposure (fixed in the harness). The subsequent run reached a cleared PowerShell
prompt but captured **zero** red/Sixel and blue/Kitty pixels. Artifact:
`build/msvc-dynamic-release/test-data/inline-images-d5423a85b3dc435babbd5d21bd9720e2/terminal-images.png`.
The session exited cleanly and no direct child remained. This is a failed
full-app acceptance result, not evidence that local image support is complete.
Investigate the native ConPTY forwarding path before claiming local compatibility;
direct engine/Qt tests do not cover its control-sequence filtering.

A subsequent isolated transport comparison confirmed this boundary. A child
process enables `ENABLE_VIRTUAL_TERMINAL_PROCESSING`, writes fixed Sixel and
Kitty sequences followed by a completion marker, and exits. With the inbox
ConPTY, the marker arrived but neither image sequence survived (144 output
bytes). With Microsoft's signed ConPTY package `1.24.260710001`, both sequences
arrived intact (113 output bytes; the focused test passed in 3085 ms). This
probe bypasses shell line editing and the renderer. The optional
`ZTERMY_CONPTY_PROBE_PACKAGE` CMake setting builds an isolated, excluded-from-all
comparison executable; it does not switch the production backend or change
release packaging. Production integration, single-executable distribution,
host lifecycle regression checks, and full-app image acceptance remain pending.

The production transport now uses embedded, hash-pinned Microsoft binaries
(ADR 0131), extracted into a versioned user cache before GUI construction.
An actual dynamic-Release PowerShell session passed `--terminal-image-smoke`:
10,930 red/Sixel pixels and 1,024 blue/Kitty pixels, exit 0. The saved capture
was inspected, and no owned application/console-host process remained.
Artifact: `build/msvc-dynamic-release/test-data/embedded-images-3e76e9347fad4e928b4227061c299427/terminal-images.png`.
All six transport cases passed (plus init/cleanup, 15.371 s), covering image
forwarding, ordinary output, fresh environment, parallel close and exit events.
The ordinary-output test originally stopped after eight arbitrary pipe reads;
new startup/control fragmentation exposed that invalid assumption. Reading to
the completion marker under a byte cap fixed the harness without weakening
the expected output. Static single-EXE validation subsequently passed with the
same red/blue pixel counts and exit 0:
`build/single-exe-image-check-c2c8133846374e2fb583d940678230d3/data/terminal-images.png`.
Only `ztermy.exe` was placed in that new directory. Focused static tests also
verified that the loaded DLL/host cannot be opened for writing while pinned and
that parallel consoles leave no owned shell/host running.

The first static run crashed before session startup. A matching Release PDB
located the access violation in `AppController::applicationSettingsDefaults`;
its object file was from September 22 while its class header had changed.
Ninja recorded zero header dependencies for it and twelve other existing
project objects. Invalidating those exact generated objects and rebuilding
resolved the crash; no application-state workaround was added. The new
`repair_msvc_ninja_dependencies.ps1` audits this historical build-tree condition
read-only by default. Both current build trees now report zero affected existing
objects. Broader image protocol/resource-policy acceptance remains unfinished.
The inline behavior follows modern compatibility described by
[xterm's sixelScrolling documentation](https://invisible-island.net/xterm/manpage/xterm.html):
scrolling is enabled by default and is the inverse of DECSDM (private mode 80).
The old DEC chapter's prose is not a safe substitute for this interoperability
check. Broader interoperability evidence remains necessary.

The framer now reports bounded complete CSI observations after forwarding their
bytes, plus RIS reset observations. This lets the adapter track Sixel
modes without consuming or duplicating the original terminal controls. Tests
exercise every input chunk size, raw C1 and seven-bit introducers, C0 embedded
inside CSI, cancelled/oversized/incomplete sequences, UTF-8 continuation bytes,
and pseudo-controls inside other control strings. A new C1 CSI can replace an
unfinished CSI without losing its mode observation.

Unicode placeholder rendering now passes direct engine and Qt item checks,
including inherited coordinates, overwrite, resize and snapshot release. A
fixed UTF-8 native child probe also preserves the placeholder and its foreground
ID through ConPTY. However, the extended full-application PowerShell smoke still
produces zero green placeholder pixels (red Sixel and blue Kitty remain visible).
This is an unresolved end-to-end failure, not completed Unicode compatibility.
Artifact: `build/msvc-dynamic-release/test-data/unicode-images-6c912f54f51d4e11a4dd4a7331527b15/terminal-images.png`.

Still pending: storage metadata admission limits (pixel-byte limits alone do not
bound image/placement counts), Unicode placeholder full-app acceptance, IME/selection-overlay
priority around above-text images, resource/performance policy, and full-app
runtime evidence. The current patch is not a claim of complete Kitty or Sixel
support. Reference:
[Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/).
