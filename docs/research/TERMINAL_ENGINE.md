# Terminal engine candidate assessment

Status: 2026-09-25 terminal polish and bounded protocol/performance audit validated;
remaining product gaps are listed below, not advertised as implemented.

## Outcome

The first terminal-state implementation uses `libghostty-vt` through a
ztermy-owned C++23 interface and C ABI adapter. Contour remains the fallback
C++ engine candidate. Windows Terminal is used as a correctness and
Windows-integration reference rather than imported as a library.

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
| OSC title, working directory, BEL, ENQ, desktop notification and progress | The corresponding optional C callbacks are not registered. This says nothing about support for unrelated sequences handled internally by libghostty. | Prioritize only from a concrete shell/app workflow and define presentation, trust and rate limits before wiring effects. OSC 0/2 title, if added, must be transient per Pane: `AppController::setTerminalTabTitle` renames and persists the entire workspace, so using it for shell output would overwrite the user's explicit tab name and restore intents. |
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

1. Reproduce the reported no-ligature drift on the affected font/DPI combination
   using the visual-only procedure above; it has not been declared fixed.
2. If a concrete workflow needs them, design transient OSC titles and shell
   progress/notification presentation without overwriting user-owned names.
3. Treat inline images and SGR text blink as separate product decisions with
   memory/accessibility budgets. They are not part of the shipped capability
   claims of this change.

No engine replacement or broad rendering rewrite is justified by the measured
results. The actionable failures in this work were host integration and
presentation behavior, which remain under ztermy's control with the current
Ghostty adapter.

Primary protocol references: the pinned
[`GhosttyTerminalOption` header](https://github.com/ghostty-org/ghostty/blob/ae8727401d8c549671c36cdc326a94f47c94b635/include/ghostty/vt/terminal.h)
defines the callback boundary; Ghostty's
[VT reference](https://ghostty.org/docs/vt/reference) describes the upstream
sequence surface, which may evolve independently of this pin.
