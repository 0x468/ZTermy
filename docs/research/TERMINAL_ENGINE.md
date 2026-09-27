# Terminal engine candidate assessment

Status: 2026-09-27. Core polish and multiple bounded terminal/window milestones
have evidence below; the overall terminal/window goal remains in progress.

## Current acceptance ledger

This ledger supersedes earlier checkpoint wording. Historical measurements below
remain evidence for their stated workloads, not claims of universal parity.

| Goal area | Current state | Remaining boundary |
|---|---|---|
| Single-Tab multi-Pane close | Measured and optimized: 4/8 running local panes, synchronous stage timings and a post-call GUI heartbeat; two before/after runs | Idle-local evidence does not promise frame-perfect teardown for all output sizes or SSH |
| OSC/manual title ownership | Implemented, persisted, controller-tested and exercised through local PowerShell/ConPTY | No separate live-server claim; do not reopen completed title work merely because other areas remain |
| Progress, notifications and SGR blink | Implemented; window-scoped routing, light/dark main/detached status captures, focus preservation and blink pixel/layer checks recorded | Native system taskbar/toast is not implemented or claimed |
| Kitty/Sixel | Bounded static rendering, rejection recovery, local/loopback-SSH slow-GUI coalescing, effects policy and Chafa 1.18.3 static PNG interoperability pass; host allocator removes measured growth through 12 D3D11 replacement rounds; 128-MiB shared stored-raster budget enabled | Not full Kitty conformance or a total-process memory cap; retain documented protocol/workload boundaries |
| Independent windows and Profile icons | Multi-Tab ownership, persistence/switches, native exact-Pane drop, search isolation and zoom; local focus/resize/swap/new/close shortcuts and restored Profile icon captures pass; light/dark status presentation and native Snap hover flyout verified | Physical mixed-DPI/hot-unplug acceptance remains; Snap tile selection is not implied by hover capture |
| Schemas and documentation | Application schema 40/workspace 10 migrations and translations have owning checks | Keep the current acceptance summary consistent with later evidence; no release/full-matrix claim |

The owner approved cross-session image budgeting: reclaim older non-visible
images first, protect visible content in every pane/window, and reject new
admission when no safe victim exists. Native atomic admission accounting and
non-blocking cross-engine reclamation and per-screen visibility protection are
implemented. Focused local/loopback-SSH checks pass; eight-engine sampling informs
the 128-MiB application startup limit. The post-enablement D3D11 image scenario
passes; broader concurrent-window visual/platform acceptance remains. See ADR 0130;
this is not a total-process memory limit.

### Remaining physical-display acceptance

The 2026-09-27 closing audit rechecked current application schema 40, workspace
schema 10, the title-policy/restore fixtures and recorded single-workspace
4/8-pane close measurements. Current localization validation passes 2,404 entries;
all 94 QML files pass format checks and qmllint. These checks do not replace the
two hardware-dependent cases below. Do not repeatedly rerun the single-display
suite while waiting for suitable hardware.

Use local disposable sessions; no production SSH connection is needed:

1. **Mixed DPI:** use two physical displays with different Windows scale factors.
   Create a main-window Tab with split panes and a separate multi-Tab window.
   Move the separate window across displays in both directions; maximize,
   restore, then drag a pane back onto a specific main-window pane. Pass if text,
   cursor, selection and pointer hit targets align, the intended pane receives
   the drop, and no session restarts or wrong-window action occurs. Restart with
   both layout restore switches enabled and verify both windows' selected Tabs,
   normal bounds and maximization. Record scale factors and result.
2. **Display removal:** place the independent window on the secondary display,
   then remove that display while the application is running. Pass if the
   window remains reachable on the remaining display, its normal geometry can
   be restored, and all Tabs/panes/sessions survive. Exit and restart with that
   display still absent; verify visible placement and correct selection again.
   Existing absent-screen metadata tests cover only this last startup portion,
   not the live display-removal event.

Source inspection finds explicit screen selection/clamping in startup
`WindowControl::restorePlacement`; there is no application-owned
`screenRemoved`/`WM_DISPLAYCHANGE` handler. Live relocation therefore also depends
on Qt/Windows behavior and must not be claimed from startup tests. The current
single-display machine cannot establish these physical outcomes. Owner feedback
or a suitable display setup is needed to close this acceptance boundary.

### Shared native image admission, 2026-09-27

Inventory collection now scans image records, placements and Unicode
placeholders once, using a bounded temporary ID-to-record index. It no longer
repeats the placement/viewport scan for each image. In
`image-inventory-scaling.txt`, 4096 virtual prototypes with only two displayed
placeholders are classified correctly; ten collections total 925 microseconds
on this machine's dynamic Release. This is a single workload measurement, not a
whole-terminal speedup claim. The four relevant behavior cases pass (six QtTest
passes including setup/cleanup), and the added C++ case passes clang-tidy.

Inspection confirms local sessions have read/write workers serialized by
`m_engineMutex`, while SSH uses a command-woken I/O loop. The queue-and-wait plan
has been replaced by engine-level serialized access and try-lock-only foreign
reclamation. See ADR 0130 for the updated lock/lifetime contract.

`image-budget-reclamation.txt` passes all 73 owning-module checks after gating
23 engine state entry points and teardown. A real three-engine admission case
first rejects at capacity while every existing image is visible, then succeeds
after one image scrolls into history: only that historical raster is reclaimed;
other visible images, text and cursor state survive. A separate held-gate test
proves a busy foreign engine is skipped without waiting and becomes eligible
after release. The later `image-budget-screen-policy.txt` also passes all 73 checks
after aligning per-screen admission: nine visible images fill 32 MiB, the next
upload receives ENOMEM without losing any, and scrolling them into history allows
admission by reclaiming only the oldest required raster. Equal-sized replacement
and shrink accounting still pass. Reapplying the dependency patch preserves all
six checked file hashes; C++/Zig format checks pass.

`image-budget-local-gates.txt` exercises 120 real ConPTY input samples with queue
P95 histogram bound 100 microseconds and maximum 124 microseconds. Its 80-image
stalled-GUI case builds one pending snapshot and delivers two on recovery.
`image-budget-ssh-gates.txt` repeats the 80 replacements over a real loopback SSH
connection, delivers two recovery frames (169 ms burst/recovery interval), and
passes the final synchronized frame on disconnect check. Each report has two
behavior cases plus setup/cleanup, no skips. These are post-change functional and
latency checks, not an A/B speedup claim or a remote-network benchmark. Test
sessions and the loopback server exit through their cleanup paths.

The additional `image-budget-local-short-soak.txt` samples 1,433 interactions over
30 seconds. All six five-second windows retain a 100-microsecond queue P95
histogram bound; the maximum individual sample is 1,541 microseconds. Snapshot
construction averages 0.144 ms (maximum 0.926 ms); stop takes 26 ms and handle
count falls from 201 to 188. This short non-image interaction workload checks for
immediate serialization regressions, not long-duration or multi-session pressure.
The three modified C++ implementation/test translation units pass clang-tidy with
warnings treated as errors, and `git diff --check` passes.

`shared-image-budget.txt` passes five focused behavior cases (seven QtTest passes
with setup/cleanup). Two real engines compete from barrier-synchronized worker
threads for four bytes: exactly one stores its raster, the other receives the
identified Kitty ENOMEM reply. Same-sized replacement works at capacity, larger
rejected replacement keeps the predecessor, and deletion/session destruction
returns the charge even while immutable snapshots remain alive. A separate
24-byte case proves Sixel and Kitty cannot bypass each other's stored-pixel
charge and can recover admission after the other session releases it.

The application now installs a 128-MiB shared stored-raster limit before session
creation. Standalone engine consumers retain explicit startup configuration.
Per-screen byte limits remain unchanged; visibility protection now
also governs their eviction. Cross-engine maintenance is serialized through the
victim's gate and skips busy engines rather than waiting for a foreign worker.

### Eight-engine budget sizing, 2026-09-27

The opt-in `ztermy_terminal_image_memory_probe multi <rounds> <MiB>` uses eight
concurrent engine producers, four distinct 1024-by-1024 RGBA images per engine
per round, and one retained UI-style snapshot per engine. Before the next round,
48 newlines move all old placements out of the 24-row viewport. It records native
stored bytes separately from private memory and heap allocations. Zero MiB means
unlimited for this diagnostic process only. It is not a Qt Quick/GPU measurement.

`image-multi-full-scroll-{0,64,128}.csv/.log` compare three rounds. Unlimited
native storage rises to 256 MiB and sampled private memory reaches about 416 MiB;
64 MiB rejects half the images even in the first all-visible batch. The 128-MiB
run retains the 32-image working set with sampled private memory around 288 MiB.
Deleting native images leaves retained snapshots alive as expected; releasing
snapshots and destroying engines returns stored accounting to zero, with roughly
13–14 MiB private memory remaining in this small diagnostic process.

`image-multi-eight-{128,192}.csv/.log` extend to 256 uploads. At 128 MiB, six
uploads are rejected; at 192 MiB, one is rejected. Both preserve visible images;
the larger budget increases sampled private memory from roughly 280–288 MiB to
348–352 MiB but cannot eliminate contention rejection. Candidate engines may be
busy and other producers may consume freshly reclaimed space first. This is an
intentional nonblocking boundary, not evidence that the budget is leak-free for
every workload. Choose 128 MiB as a bounded default rather than raising the limit
to hide contention; retained snapshots, decode scratch and GPU allocations remain
outside that accounting. The first 24-newline exploratory run left some old
placements visible and is not used as the fully-offscreen comparison above.

After application enablement, isolated D3D11 run
`image-pressure-d3d11-45ba391026044e94989deb22e9ce5e9b` exits 0. Kitty, Sixel and
Unicode image pixel checks pass at full/reduced/off effects tiers, and maximum
GUI heartbeat gap is 13 ms. Four post-delete private-memory samples are 291.39,
298.19, 281.08 and 269.57 MiB; dedicated GPU memory is 42.35 MiB in each deletion
phase. This confirms no monotonic growth in that replacement workload, not a
whole-process 128-MiB ceiling. The scoped startup call and probe/budget code pass
clang-tidy; test application and its tracked children exit without leftovers.

### Image replacement capacity boundary, 2026-09-27

The next budget prerequisite now has a worker-only native inventory and guarded
eviction interface. Records distinguish primary/alternate screens, screen
generation, image generation, stored bytes and visibility aggregated across all
placements. Eviction rechecks those identities and current visibility, uses the
engine's pin-aware deletion, and marks image storage dirty. It does not feed VT
commands or switch the active screen to perform deletion.

`image-eviction-visibility.txt` passes six related behavior cases (eight QtTest
passes including setup/cleanup). The added case exercises a stale offscreen
candidate after another visible placement is added, a replaced image generation,
Unicode placeholders, and identical IDs in primary/alternate screens. The shared
admission coordinator was not connected at this checkpoint; the later shared
admission section above supersedes that integration status. This interface alone
does not enable automatic cross-session eviction.

An independent full-budget case exposed unnecessary eviction before introducing
the shared budget: eight 4-MiB RGBA images filled the current screen limit;
replacing the newest ID with an equal-sized raster left only six placements.
The old storage admission counted the full replacement before subtracting its
predecessor, and eviction continued when reclaimed bytes exactly met demand.
The mechanical pinned-dependency patch now counts the replacement's net increase,
excludes its predecessor from eviction candidates, and accepts exact reclamation.
This does not yet provide process-wide budgeting or visible-image protection.

`image-replacement-before.txt` records the expected failing 8-versus-6 assertion.
`image-replacement-after.txt` passes seven relevant behavior cases (nine QtTest
passes including setup/cleanup), including equal-sized replacement, shrinking
and refilling the exact released capacity, immutable snapshots, metadata bounds,
rejected upload recovery, mixed Sixel/Kitty output and screen/scroll changes.
Both reports are under `build/msvc-dynamic-release`. The added C++ test passes
clang-tidy; no full regression or new UI acceptance is claimed.

### Physical display and launch evidence, 2026-09-27

Read-only display inventory reports one active display, `DISPLAY1`, 2560-by-1440,
96 effective DPI / 100% scale, with a 2560-by-1392 work area. Consequently this
machine cannot currently prove physical mixed-DPI transfer or monitor hot-unplug
behavior. Existing absent-screen startup fixtures are still useful, but are not
substitutes for those physical checks.

The attempted standalone visual/Snap inspection did not start successfully:
the shell tool ended with code -1, and desktop inspection showed the security
product blocking hidden PowerShell execution. No permission button or protection
setting was changed, and no alternative launch mechanism was used to bypass it.
The copied fixture directory is
`build/msvc-dynamic-release/test-data/visual-window-check-20260927`; the attempted
launch is not visual acceptance evidence. The owner subsequently authorized
the test launch and temporary allowlisting. Run
`native-drag-events-54ea5230c75a434d9dcf6d6427493af6` then started and terminated
normally, so launch permission is no longer the blocker. Its app exit code was
1: only the shrink-to-original-layout check reported false. That check compared
the entire live node model, including session fields, rather than just layout.
The check now compares recursive node identity, kind, orientation and ratios.
Follow-up `native-drag-events-fce5b425767548c88d5666be21af065d` exits 0 and records
restored geometry=true but complete live model equality=false, confirming the
old assertion mixed unrelated live fields into layout acceptance. All scoped
native pane actions and exact-Pane transfer checks pass; no production behavior
was changed for this assertion repair. Neither run proves Snap flyout or physical
mixed-DPI acceptance. Both test applications exited and their runner found no
remaining direct child processes.

### Detached new-Tab entry consistency, 2026-09-27

The detached title-bar plus button still called start/insert directly, bypassing
the shared `openLocalTab` logic used by keyboard and context-menu entry points.
With a different first main-window Tab as a sentinel, runtime activation of the
plus button changed the main-window selection even though the keyboard path
passed. Before-fix run `native-drag-events-0e632d535baf4ff48a6acc558e5a8ece`
failed only the added selection-isolation assertion. The button now uses the
shared entry point, preserving main selection and consistent insertion order.

After-fix run `native-drag-events-6621e9cbd5db40bfba8cea70c6ff8a32` exits 0:
plus-button isolation, native next/previous/new/close/split shortcuts, scoped
search/rebinding and exact-Pane cross-window drag pass. The plus-button check
invokes the actual QML `activated` signal; it is not a separate mouse-hit-test
claim. Native keyboard/drag checks still use guarded Windows input. Both runs
use isolated data and terminate their own sessions. QML formatting/lint and
`src/main.cpp` clang-tidy pass. Physical mixed-DPI/display-disconnect behavior
remains separate acceptance work.

### Detached pane action acceptance, 2026-09-27

The expanded native check found two issues. Immediate focus transfer ran before
the coordinator's deferred workspace synchronization and could reactivate the old
pane. The detached action now queues its focus request after that synchronization
and checks that the window remains active. Also, `moveTerminalPane` still rejected
every target outside `main`, which prevented a detached window from swapping its
own panes. It now accepts valid target layouts regardless of owning window and
retains the existing transactional transfer validation.

Run `native-drag-events-9832c2530c784caabf25c24970341d83` passes real Alt+Right /
Alt+Left focus changes with a focused TerminalItem, Alt+Shift+Right / Left ratio
change/restoration, and temporarily bound F9/F10 session exchange/restoration.
It also retains the main-window isolation, plus-button, search, Tab and exact-Pane
drag assertions. Session exchange is checked by session IDs, not layout node IDs:
the feature exchanges the sessions carried by the existing nodes. Test keyboard
injection explicitly marks Windows arrow keys as extended keys; earlier injection
without that flag was invalid evidence for directional shortcuts.

The intermediate run ending `46d91769726a424ebc415d08d38316c4` passes focus and
resize after the focus correction but still fails exchange. The diagnostic run
ending `986eab5eb54c4ad8998329480965edc6` confirms the exchange command arrives and
is rejected by the controller. Temporary product logging was removed before the
passing final run. `detached-pane-actions-tests.txt` records three relevant
controller cases / five QtTest passes, including session-start/stop counters and
transaction rollback behavior. QML quality checks and clang-tidy for the transfer
controller and runtime entry point pass. No full regression or physical
multi-display claim is made.

### Independent image encoder acceptance, 2026-09-27

Used the official portable [Chafa 1.18.3 Windows build](https://hpjansson.org/chafa/download/)
only under ignored `build/tools`; it is not a product dependency or packaged asset.
The downloaded ZIP SHA-256 was
`3D7B43CAB1C9D9116024AB48664DB8910E3EABF0912720828D5E7AD1B80194FA`.
The input is an original opaque 320-by-160 PNG with red/green/blue vertical
bands of widths 107/106/107. Relevant options are `--format kitty` or
`--format sixels`, `--probe off --animate off --threads 1 --exact-size on
--size 40x10 --view-size 100x40 --dither none`.

The explicit `--terminal-image-smoke --data-dir <isolated-directory>` test entry
can run an `external-images.ps1` in that directory instead of its built-in VT
fixture. The script clears the viewport and invokes Chafa from the local shell;
normal application startup never checks or runs this file. Protocol output then
travels through ConPTY, the session worker and the actual viewport. The screenshots
were inspected, not only pixel-counted. All full/reduced/off effects checks passed:

- `chafa-raster-sixel-578040cdde694bebaffc37f23520a5e3`: 320-by-160 output,
  red/green/blue counts 17120/16960/17120.
- `chafa-raster-kitty-ce0d8b2fd199488a813a31751269daf3`: Chafa requests 32 columns
  by 8 rows; the 8-by-16 cell grid correctly displays 256-by-128 pixels,
  red/green/blue counts 10880/10752/10880 (two interpolated color boundaries).

Both directories are under `build/msvc-dynamic-release/test-data`; each contains
its script, logs and three `terminal-images-<tier>.png` captures. Processes exited
successfully and the runner found no remaining direct children. A dependency-free
engine regression preserves the cell dimensions from Chafa's empty initial Kitty
packet across payload-only continuation packets. Optionally setting
`ZTERMY_TEST_KITTY_FIXTURE` to the saved `chafa-output.vt` exercises that independent
encoder stream against the same 320-by-160 source / 256-by-128 placement oracle.

The earlier SVG attempt failed the fixed color-area threshold. Decoding Chafa's
actual Kitty payload explained why: its 320-by-160 raster contained only
192-by-96 colored pixels plus 32768 near-transparent black pixels (alpha 1).
Both direct engine decoding and application placement were correct; no product
scaling change or lowered threshold was made to hide the mismatch. PNG removes
this encoder-side SVG rasterization variable. This is static, explicit-format
local-client evidence, not automatic capability probing, SSH, animation or native
GPU memory-pressure acceptance.

### SSH image burst and stalled GUI, 2026-09-27

`coalescesImagesOverLoopbackSshWhileGuiIsStalled` uses the existing single-client
Paramiko fixture on an ephemeral loopback port. It does not connect to a real
host or expose a remote OS shell. After one seed image, the server sends 80
128-by-128 RGB replacements of the same image/placement, a final text marker and
CPR. The test blocks the GUI event loop while waiting only on the server's
control pipe. The server reports completion only after receiving the CPR reply,
proving the SSH worker consumed the entire burst rather than merely accepting
bytes into a socket. Snapshot deliveries must remain unchanged during this wait.

On resuming the event loop, the final shade (81) and final marker both arrive,
the original image's weak reference expires, and no more than two snapshots are
delivered. The measured run used two deliveries and 157 ms from burst start
through recovery. This is not a throughput benchmark or a process/GPU memory cap.
The test deliberately retains only the latest snapshot, unlike a signal spy that
would itself retain every historical image.

Evidence: `build/msvc-dynamic-release/ssh-image-stall-tests.txt`, three behavioral
cases / five QtTest passes including setup and cleanup, no skips. Adjacent checks
cover synchronized output over the same SSH fixture and the final frame before
disconnect. Reproduction uses the previously documented Paramiko 5.0.0 directory
and `ZTERMY_TEST_SSH_FIXTURE_PYTHON`; only these named tests need running. The
server exits normally, and session shutdown joins the worker. No production
queue change was necessary for this case.

### Full-app image pressure: attribution and host-allocator correction, 2026-09-27

The opt-in external smoke workload now supports `external-images.wait`: it waits
up to 45 seconds for the isolated script's `external-images.done` while measuring
an 8-ms GUI heartbeat. Without this handshake, the screenshot check could finish
after the first image and prematurely terminate a longer workload. The scripts
and sampler are retained under `build/image-pressure-tools`; runs have separate
data directories, phase markers, logs, screenshots and `memory.csv`.

Workload: four rounds of 40 alternating red/blue 512-by-512 RGB uploads replacing
one Kitty image ID and placement ID, 60 ms between uploads, then deletion of all
images and a three-second observation period. The final 240-by-96 three-band
image validates continued rendering at full/reduced/off effects tiers. The
sampler measures the ztermy PID only, not its PowerShell child, using process
private bytes and Windows GPU process-memory counters. Missing GPU counters
remain missing, not zero. GPU polling is slower than the 100-ms process samples;
`gpuSample` marks fresh GPU observations.

| Backend / last sample after deletion | Round 1 | Round 2 | Round 3 | Round 4 |
|---|---:|---:|---:|---:|
| D3D11 private MiB | 337.25 | 385.03 | 440.50 | 499.00 |
| D3D11 dedicated GPU MiB | 43.00 | 43.00 | 43.00 | 43.00 |
| Software private MiB | 223.55 | 281.83 | 340.38 | 400.51 |

D3D11 was confirmed in Qt's runtime log using the RTX 4060 Ti, not inferred
from the requested environment. Its maximum GUI heartbeat gap was 18 ms;
software's was 17 ms. Both completed the final image checks and exited 0, with
no remaining owned children. These functional passes do **not** pass the memory
acceptance gate: CPU private memory continues growing across equivalent rounds
on both backends. No heap-root or allocator attribution is proven yet.

Evidence directories under `build/msvc-dynamic-release/test-data`:
`image-pressure-d3d11-89e4b3e5529945a2ba948213f96246c5` and
`image-pressure-software-b70a34038f794c5c996ccc817dd25429`.
Earlier two-round D3D11 evidence is retained as well. Its first attempt failed
the final pure-color pixel threshold because a three-pixel image was enlarged
with smooth interpolation; the final fixture uses a one-to-one raster instead,
without changing rendering or lowering thresholds.

The isolated image-memory probe now has `rgb` and `streamed` modes. Both perform
16 replacements per cycle; `streamed` also captures snapshots during incomplete
multipart uploads. Across ten cycles, RGB private bytes settled at 13,746,176;
streamed settled at 15,974,400 from cycle 3 onward. Heap busy bytes remained
1,313,049 and 1,313,069 respectively. Evidence is in
`build/msvc-dynamic-release/image-probe-rgb.csv` and `image-probe-streamed.csv`.
Two additional modes serialize feeding with snapshot creation on a persistent
worker (`cross-thread`) or alternate snapshot creation between that worker and
the feeding thread (`alternating`). Across ten cycles, their private bytes settle
at 27,807,744 and 16,138,240 respectively; live heap bytes settle at 1,354,051
and 1,354,047. Reports are `image-probe-cross-thread.csv` and
`image-probe-alternating.csv` in the same build directory. These are controlled
allocation-thread tests, not the application's complete session scheduling.

Full-app software runs with the viewport hidden still grow after deletion:
219.66, 278.13, 339.13 and 396.02 MiB. Temporarily bypassing the output-observer
fanout as well produces 221.95, 279.68, 338.22 and 398.12 MiB. Evidence directories
end in `1ca46bd537de48459e877093a4b3a7f8` and
`36d72c95293f4c0eb540d79dc0f2db8e`. Both restore the viewport for successful final
captures. The observer bypass has been removed and the application rebuilt.
Thus active painting and that output fanout are not required to reproduce the
slope; this does not exclude snapshot/status consumers or allocator retention.

Allocation-stack profiling with SDK UMDH 10.0.22621.755 failed to collect stacks
despite verified `+ust` on a separately named diagnostic EXE. Microsoft documents
this class of [Windows 11 SDK UMDH failure](https://learn.microsoft.com/en-us/troubleshoot/windows/win32/umdh-windows-11-sdk-not-work-fine).
The instrumented run `image-pressure-software-08a85056c980429a8292612fd16d0ca3`
is not allocation attribution or normal-latency evidence. The diagnostic flag
was cleared and verified as zero; no tracing was enabled for `ztermy.exe`.
The first CDB summary attempt also lacked ntdll symbols and is not valid heap
evidence. After retrieving the matching Microsoft ntdll symbols into the ignored
build cache, run `image-pressure-software-bc738279aa1144648a14dda0097d58d2`
provides valid `summary-deleted-1.txt` / `summary-deleted-4.txt`. Process private
memory rose from 223.30 to 400.14 MiB, while the NT heap's committed KiB changed
only from 99596 to 100372. Read/write committed regions grew from 215.199 to
391.812 MiB, mostly outside the debugger's recognized heap regions. Debugger
attachment affects latency, so its heartbeat is not normal-runtime evidence.

A temporary allocator A/B changed only the allocator passed to
`ghostty_terminal_new`: an MSVC aligned-allocation/free vtable, declining optional
in-place resize/remap. All other Ghostty handles retained their original
allocators. Run `image-pressure-software-a5ee64ca4c7840f7861873f877f0bc3b` used
the same hidden-viewport workload without debugger instrumentation. After-deletion
private MiB were **177.11, 175.42, 176.34, 177.80**, instead of continued growth;
maximum GUI heartbeat gap was 21 ms and all final image checks passed. This
isolates the terminal-state allocator path as a practical mitigation point, not
an exact allocation-site or universal no-leak proof. The temporary code was
removed and the normal application rebuilt; this is not yet a shipped fix.

The production adapter now uses process-lifetime `GhosttyHostAllocator` with
paired C++ aligned nothrow new/delete, rejecting unrepresentable requests and
declining optional resize/remap without changing the original bytes. It is passed
only to `ghostty_terminal_new`; terminal destruction retrieves the same retained
allocator in the pinned C bridge. The bridge passes log2 alignment, unlike the C
header's byte-alignment prose. Tests therefore include alignments through 64 KiB,
zero length, impossible requests, preservation after declined resizing, and the
actual `ghostty_alloc`/`ghostty_free` bridge. An existing `Impl` constructor's
incorrect `noexcept` was removed because its image-cache container can allocate.

Relevant results: `host-allocator-engine-tests.txt` has 66 passes,
`host-allocator-local-tests.txt` 4, and `host-allocator-ssh-tests.txt` 5, including
setup/cleanup; no failures or skips. This is owning-module plus focused session
verification, not full regression.

With the production adapter and visible viewport, confirmed D3D11/RTX 4060 Ti run
`image-pressure-d3d11-3c0dbff1da75430eaa87b1a6e0bc4de5` records after-deletion
private MiB **275.64, 277.96, 278.52, 276.61**, compared with the earlier
**337.25, 385.03, 440.50, 499.00**. Dedicated GPU memory returns to **43.25 MiB**
each round. All final effect-tier image captures pass, with maximum GUI heartbeat
gap 16 ms. This validates elimination of the measured slope for this workload,
not a universal memory ceiling or a promise that all baseline memory is necessary.
The matching visible software-backend run
`image-pressure-software-a0a782879b724f8bbe16ad09d09eb638` records **176.60,
175.30, 176.06, 176.03 MiB** after deletion (previously **223.55, 281.83,
340.38, 400.51**), with a 14-ms maximum heartbeat gap and successful final
captures at all three effects tiers. Both runs exited normally with no remaining
owned direct children. No debugger or tracing configuration was enabled.
An explicit `external-images.extended` marker raises only this smoke workload's
deadline to 180 seconds. The runner's `-Extended` case uses twelve rounds / 480
replacements, retaining the same per-frame size, cadence and deletion interval.
Confirmed D3D11 run `image-pressure-d3d11-a5700cfff81647798e79e34fff21a60e`
completed in approximately 100 seconds. After-deletion private memory settled
from the initial 326.35 MiB to 281.18 MiB in round 12; rounds 10/11/12 were
280.41/280.07/281.18 MiB. Dedicated GPU memory returned to 42.98–42.99 MiB each
round, maximum GUI heartbeat gap was 17 ms, final captures passed, and the process
exited normally. This longer run does not reproduce the original cumulative
slope. The cross-session budget decision and other workloads remain separate.

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
with every mainstream terminal. The following is the 2026-09-26 scope checkpoint;
use the current acceptance ledger above and the subsequent evidence for status:

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
   detached-window routing was added later (see window-restoration evidence).
   System taskbar/toast presentation remains outside the implemented in-app path.
3. Approved on 2026-09-26: both Kitty and Sixel inline images, plus SGR text
   blink, with memory/accessibility budgets. These now have implementation and
   focused evidence below; full protocol conformance and a new release are not claimed.

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
progress. This checkpoint predates detached routing; later window-scoped
integration and acceptance below supersede that earlier limitation.
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

That failure is now reproduced and fixed: the dependency's integer source
rectangles rounded the one-pixel image's per-row source slices to zero. A new
regression failed with zero placements before the fix; preserving fractional
geometry produces the expected quarter-pixel slices without enlarging stored
pixels. Existing coordinate inheritance, replacement, deletion and scrolling
checks also pass. Hardware Qt item checks cover the magnified multi-row case at
normal and 125% scale. The full PowerShell/ConPTY/application smoke now exits 0
and its inspected screenshot contains red Sixel, blue Kitty and green Unicode
images. No application or direct child remained after the smoke.
Artifact: `build/msvc-dynamic-release/test-data/unicode-fractional-bcca95cc7d794cfba0dd3f9ea7eaae67/terminal-images.png`.

Storage now independently caps images and placements at 4096 records per screen.
An adversarial single-pixel stream reproduced the missing limit before the fix.
The protocol-level test now verifies rejection, replacement while full, deletion
and resumed admission for both maps; ordinary text still works afterwards.
Replacing an explicit placement releases the previous tracked pin only after
success. The focused engine group (10 cases plus init/cleanup) passes in 838 ms,
and all four image-rendering cases pass. Long-running memory profiling of repeated
replacement is still needed; a count-limited snapshot alone is not proof of it.

Three hardware-rendered checks reproduced images covering IME preedit and
selection backgrounds (above-text images covered both; below-text images covered
selection). Protected local-interaction rectangles now preserve these regions
without hiding the rest of the image. The checks include Chinese preedit,
cancellation/selection clearing and restored image pixels, at normal and 125%
scale. Adjacent tests confirm no premature or duplicate IME input. These Qt
input-method event checks do not replace native candidate-window acceptance.

Still pending: broader Unicode interoperability, resource/performance policy, and full-app
runtime evidence. The current patch is not a claim of complete Kitty or Sixel
support. Reference:
[Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/).

### Image lifetime measurement (2026-09-26, dynamic Release)

Build the opt-in `ztermy_terminal_image_memory_probe` target through
`msvc-dynamic-release`, then run the resulting EXE with the matching Qt runtime
on PATH. It is excluded from normal builds and CTest, uses only synthetic pixels,
and starts no shell. CSV output includes private bytes, working set and Windows
heap busy bytes/blocks. Heap walking is diagnostic-only and is not used in the
application; these elapsed times include measurement overhead.

The first three short runs showed a flat private-byte count between 5,000 and
100,000 replacements of the same placement (5,586,944 / 5,607,424 / 5,582,848
bytes respectively). Retaining sixteen old 1-MiB image snapshots increased memory
as expected; releasing them expired every weak image reference. That alone did
not prove full memory recovery: repeating the cycle continued to grow private
bytes. An extended 20-cycle run, now also walking process heaps, measured:

| After deleting the image | Private bytes | Heap busy bytes | Heap busy blocks |
| --- | ---: | ---: | ---: |
| Cycle 1 | 11,431,936 | 1,702,738 | 765 |
| Cycle 10 | 15,630,336 | 1,702,738 | 765 |
| Cycle 20 | 21,274,624 | 1,702,738 | 765 |

After engine destruction, private bytes remained 19,279,872. The benchmark
still owns its fixed base64 source buffer at that point. Stable heap busy counts
and expired snapshot references narrow the investigation but do **not** prove
that all allocations are freed: direct virtual allocations and allocator caches
are outside that heap census. Isolating transport parsing, image loading and
snapshot capture, then accounting for the native allocator, was the next step.

The probe now accepts `retain`, `upload-only`, `no-retain`, `image-only`,
`metadata-only` and `iterator-only`; an optional second argument selects 1–200
cycles (default 20). `image-only` bypasses text render-state updates;
`metadata-only` also omits copying pixels; `iterator-only` repeats enumeration of
one fixed tiny image without uploading. These are diagnostic comparisons, not
alternative application behaviors. The extra native terminal in the isolated
modes makes their absolute baseline different; compare trends within each mode.

Before iterator reuse, upload-only stabilized by cycle 10 (12,775,424 bytes),
whereas no-retain grew from 11,460,608 to 21,311,488 bytes between cycles 1 and 20.
Image-only and metadata-only both grew as well, but 1,000 iterator-only operations
were flat. This isolated the problematic allocation **pattern**, not a leaked
pixel owner: repeatedly creating/freeing iterators interleaved with uploads.
The snapshot adapter now owns one reusable iterator, with reset/rebind on every
capture. The pinned default allocator may use Zig's slab allocator when libc is
not linked; we do not label all retained private bytes as live allocations.

After this change, the no-retain 100-cycle run (1,600 uploads) measured:

| Cycle, after deleting image | Private bytes | Heap busy bytes | Heap busy blocks |
| --- | ---: | ---: | ---: |
| 1 | 11,403,264 | 1,697,491 | 766 |
| 10 | 13,307,904 | 1,697,491 | 766 |
| 20 | 13,307,904 | 1,697,491 | 766 |
| 50 | 13,307,904 | 1,697,491 | 766 |
| 100 | 13,193,216 | 1,694,347 | 760 |

The retained-frame and image-only repeats likewise plateaued between cycles 10
and 20. This provides bounded-workload evidence of improvement, not a proof of
zero allocator retention or a bound on process-wide GPU/session memory.
Focused engine and Qt image/selection/preedit checks passed. Full-app local
PowerShell image smoke exited 0; the inspected capture contains all three image
fixtures: `build/msvc-dynamic-release/test-data/image-lifetime-259275fe76a34797872766a5ecdb7d52/terminal-images.png`.

### Detached window ownership groundwork (2026-09-26)

The QML coordinator now indexes native windows by the domain's `windowId`, not
workspace/Tab ID. A detached window has a reusable Tab strip, its own selected
workspace, window caption controls and the shared pane drag surface. Basic
multi-Tab grouping is implemented; complete detached-window acceptance and
restart restoration are still pending.

The controller now permits detaching an entire split tree, inserting/reordering
a Tab within an existing detached window, and merging into a chosen pane of a
detached workspace. Existing two-argument insertion still targets the main
window. Unknown destination windows are rejected without changing ownership;
failed persistence leaves both order and membership unchanged. Moving a Tab
within its current window preserves its return destination.

The split-tree detachment check failed before the change. Four focused dynamic
Release controller cases now pass (493 ms), covering session identity, preserved
tree and manual title, target-window order, specific-pane merge, invalid target,
failed save and main-window selection on detached close. These use fake session
backends and start no terminal processes. Targeted clang-tidy, formatting and
code-health checks pass. This is controller evidence only, not native-window UX
or restart-restoration acceptance.

The new `--terminal-render-smoke --workspace-transfers-only --detached-tabs-only`
runtime check opens real local sessions in an isolated data directory. On the
software backend it verified one native window for two Tabs, switching to the
two-pane Tab, independent selection while the main window changes, preserving
the window when one Tab closes, and closing only that window's group. The process
exited 0 and no direct children remained. Its inspected asynchronous capture is
`build/msvc-dynamic-release/test-data/detached-tabs-bbc01997f1e841b189362ce34332cdb2/detached-multi-tab.png`.

The first run exposed a software-renderer crash: texture children were attached
before receiving their textures, and Qt inspected them synchronously during
insertion. Cursor, image-overlay and blink nodes now initialize their textures
before attachment. The same real-window scenario and eleven focused image,
selection, preedit and cursor cases pass on the software backend. This does not
substitute for hardware/native-window acceptance.

Remaining UI work includes real mouse Tab/Pane transfer acceptance, SSH reconnect UI acceptance,
visible native Snap flyout acceptance, and persistent geometry/layout restoration.

Main and detached windows now share `TerminalRenameDialog`, retaining the
existing `Main` translation context. Each popup belongs to the window where it
was invoked. Detached Tab duplication uses the existing session duplication
operation and inserts the resulting Tab into the same window; SSH reconnect is
wired to the existing controller operation. The extended real-window smoke
verified popup window ownership, title submission, duplicate-window membership,
and the previous grouping/closing checks. It exited 0 with no remaining direct
child processes; the inspected capture is
`build/msvc-dynamic-release/test-data/detached-menu-21fa65915aa745e991f8ff68152e3a0b/detached-multi-tab.png`.
QML formatting/lint and translation call-site checks pass. No live SSH server
was contacted, so this run is not SSH reconnect acceptance evidence.

Cross-window target resolution now checks Windows z-order in the platform layer.
Invisible, minimized and cloaked windows are skipped, while an unrelated visible
window covering the point blocks the drop. Only the native window currently
being moved is ignored. Client-local Qt coordinates are mapped to physical
screen coordinates before testing the window stack. QML still determines the
specific leaf and insertion edge; native window discovery does not change focus.

The real-window smoke verified exact leaf/owner identification, no target under
a covering native window, ignoring only the moving source, and reacquiring the
target after the cover hides. It passed at normal and forced 125% scale, with
exit 0 and no remaining direct children. Evidence directories:
`build/msvc-dynamic-release/test-data/detached-occlusion-d648859fdc44474abb78291aaf2b9bd0`
and `build/msvc-dynamic-release/test-data/detached-occlusion-125-5f5b78af84344065aecf9475aa70774c`.
The scaled run logged DirectWrite fallback warnings for `MS Sans Serif`; these
are not treated as a font-rendering acceptance pass. Forced scale and target
resolution do not replace a mixed-DPI native mouse-drag acceptance run. Targeted
platform/UI clang-tidy, QML checks and code-health checks pass.

The final pre-commit software-backend smoke at forced 125% scale also passed
native maximization hit testing, pressed/released state, and the sequence
maximize, minimize, present (still maximized), restore. Window caption hover no
longer reveals every pane toolbar. Evidence:
`build/msvc-dynamic-release/test-data/detached-commit-94eac70ec12940809cf01d43c39fa7ff`.
The process exited 0 with no remaining direct children. This verifies native
hit/state handling, not visual appearance of Windows' Snap flyout.

### Remaining terminal topic execution order

Workspace schema 10 now serializes bounded per-window normal geometry,
maximization, selected workspace and screen hints (ADR 0132). Schema-9 fixtures
retain pinned names, local Shell identity, detached ownership and recovery
quarantine. Store checks cover negative monitor coordinates, round-trip state,
stale selection, malformed/fractional dimensions, duplicate owners, unchanged
files on invalid save, backups and future-schema refusal: 20 QtTest cases
including setup/cleanup passed in 158 ms. Targeted domain/store clang-tidy passed.
Geometry capture/application is now connected through the coordinator and
WindowControl. Placement updates coalesce in controller memory; ordinary layout
saves and the final shutdown snapshot persist them. Focused tests verify 100
updates without intermediate store writes, the final shutdown snapshot, screen
bounds and hidden/maximized transitions. Dynamic Release, QML formatting/lint
and targeted controller static analysis pass. Native restart evidence is recorded
below; mixed-screen acceptance remains unfinished. Persistence and offscreen
checks alone do not prove UI restoration.

The actual application now passes two-process startup/shutdown/restart checks
at normal and forced 125% scale with the software backend. The isolated fixture
contains one main Tab and two detached Tabs, with the second detached Tab selected
and its window maximized. Native maximization, main logical geometry, both window
selections, saved normal bounds and all three Tabs survived both launches; no
Shell was reopened and no direct child remained. Run
`scripts/verify_window_restore.ps1` to reproduce. Evidence directories:
`build/msvc-dynamic-release/test-data/window-restore-646eac1cabc54f7590d1d418e74d8ebf`
and `build/msvc-dynamic-release/test-data/window-restore-9ada511644f74551a54f6e917d79eac6`.
The smoke exits through the existing controller shutdown path, not by closing a
window group. Earlier external-message shutdown attempts failed and were cleaned
up; they are not acceptance evidence. Mixed-monitor, removed-screen and native
pointer-drag scenarios remain pending. Targeted main translation-unit clang-tidy
also passed.

The `-RemovedScreen` variant now covers startup with a nonexistent screen name
and both saved origins at (-90000, -90000). At normal and forced 125% scale,
windows returned inside the current work area; the detached window unmaximized
to 800x520 and maximized again before shutdown. This is startup recovery from
absent-screen metadata, not evidence of physical hot-unplug or mixed-DPI drag.

Terminal notifications now carry the originating workspace's window ID and use
the shared toast only in that window. The controller test explicitly activates
a different main-window Tab before delivering detached-session output; routing,
progress and deduplication pass (four QtTest cases including setup/cleanup).
Real QML checks deliver to each window separately and verify exclusive popup
visibility without changing the focused window. Both startup passes succeed
and leave no children. Evidence, normal and 125% respectively:
`build/msvc-dynamic-release/test-data/window-restore-573d5a2209f641f0ad92d6a67a99fa04`
and `build/msvc-dynamic-release/test-data/window-restore-507fb199cef8433b93616355c5843b0d`.
Dynamic Release, QML formatting/lint, translations and targeted C++ static
analysis passed. Detached Tab progress already uses the same status component;
broader end-to-end protocol and visual acceptance is not implied by routing tests.

The opt-in `scripts/verify_window_restore.ps1 -VisualStatus` now supplements this
with light/dark screenshots of main compact and detached expanded Tab progress
(determinate/error/indeterminate/paused), plus UTF-8/plain-text notifications in
both windows. It deliberately supplies presentation state without opening a
remote session: prior VT-fed tests cover protocol delivery. Run
`window-restore-ba822682609444ecb44de600ea0a4489` passes two native restarts, including
unchanged selection/normal bounds/maximization/topology, no child processes and
notification focus preservation. Original PNG pixel checks confirm both windows
match: light progress/error/paused RGB 112,67,164 / 220,38,38 / 217,119,6; dark
181,154,232 / 239,68,68 / 245,158,11. Chinese text and literal `<b>` text are visible
in the captured notifications. A misleading visual reading of tiny previews
initially suggested a one-frame delay; direct PNG inspection disproved it in all
four runs. Experimental warm-up/whole-window capture workarounds were removed;
no product-rendering defect is claimed. This is software-backend presentation
evidence, not proof of physical Snap UI or mixed-display transitions.

`scripts/verify_window_restore.ps1 -SnapCapture` adds a separate native hover
check. It foregrounds only the isolated detached window, verifies foreground
ownership, moves the real cursor over its maximize button, waits for the OS hover
UI, captures the screen's top-right region and restores the cursor. A QML grab
would miss this OS-owned overlay. Run `window-restore-907e8dbc649046f68edbce4bae4cf10f`
passes both restarts, exits without children, and its `native-snap-hover.png` was
visually inspected: Windows' layout chooser is present beneath the maximize
button. This proves flyout appearance, not selecting every layout tile. Desktop
captures may include content visible through a transparent window; these local
ignored artifacts must not be automatically published. Static analysis passes.

The current machine exposes only `DISPLAY1` at 2560x1440. Physical mixed-DPI
cross-monitor dragging and hot-unplug remain explicitly unverified; neither
`QT_SCALE_FACTOR` nor the absent-screen startup fixture substitutes for them.
The owner has been asked whether these two manual checks can be performed on a
dual-display setup. Keep this environment-dependent boundary separate from the
completed single-display restore and Snap hover checks.

The final code-health check caught size regressions in the engine and the growing
window smoke header. Independent native error mapping now lives in
`GhosttyError.h`, and status/Snap presentation checks in
`WindowStatusRuntimeSmoke.{h,cpp}`. No size baseline was increased: the engine is
2313 lines against its 2314-line ratchet, and the window-state header is again
below its 400-line budget. The health gate passes. Dynamic Release builds and
`terminal-helper-extraction.txt` passes all 73 owning-module checks after extraction.

The detached restore switch is now implemented (application settings schema 40,
schema-39 fixture). On startup, master-off discards saved sessions; detached-off
keeps restored Tab/Pane topology in the main window; both on retain window IDs.
Local reopen and SSH reconnect remain separate policies. All 36 application
settings cases and four focused controller cases (six including setup/cleanup)
passed. They cover the four switch combinations, no unintended local process
starts, original Shell restoration and SSH startup policy. QML/translation gates
passed (2,397 catalog entries). These are controller/configuration checks, not
native restart placement or interactive settings acceptance.

Detached Tab delegates now connect pointer movement and release to window-aware
drop resolution, with Escape/cancellation clearing the pending target. In-strip
insertion accounts for removal of the source Tab. Tearing a Tab out of an
already detached group now assigns a new native-window owner; re-detaching a
single-Tab window remains a no-op. Focused controller checks cover this distinction
and preserve original session start/stop counts.

A Windows `SendInput` drag from the first detached Tab past the second verified
the resulting controller order and completed the existing grouping/caption
smoke (exit 0, no direct children remaining):
`build/msvc-dynamic-release/test-data/detached-native-drag-3510caa9bad5416a93f79d28512ee531`.
Earlier Qt-synthesized pointer runs did not activate the drag handler and are
recorded as failed checks, not UX evidence. Cross-window gestures, overflow
scrolling and canceled drags still need their own native-pointer checks.

Main-window Tab drags now route outside their own title strip through the same
window-aware coordinator, retaining local strip reordering. An overlapping
detached window takes precedence over the main strip's geometric rectangle.
The extended smoke includes Escape cancellation and a two-window round trip,
but these new checks are **not yet accepted**. The current desktop run could
not make the source window the foreground pointer target, so a guard stopped
before sending mouse buttons. This is an unavailable native-input prerequisite,
not a passing drag check; the earlier unguarded attempt also failed. Evidence:
`build/msvc-dynamic-release/test-data/cross-tab-guarded-1f8d8f844a5d436cad9cb942d42ddd07`
(exit 1, process and direct children gone). Compilation, QML quality, formatting
and translation checks passed for this iteration. Do not bypass the foreground
guard or weaken native acceptance to make the smoke green.

Profile-owned Tab icons are now implemented (ADR 0133, SSH profile schema 9).
The name field's icon button offers six existing interface icons; old schema-8
profiles default to the terminal icon. The fixed migration fixture includes
identity/credential references, a jump route and non-default timeout settings;
after migration, the entire old JSON remains identical apart from the version
and new icon fields. All 18 store cases passed. Controller checks cover retaining
icons during ordinary Profile edits, live detached-Tab updates, invalid values,
and restoration. The same check exposed an obsolete main-window-only split guard;
it is removed, with detached workspace ownership asserted after splitting.

Real QML startup checks now seed SSH Profile references and verify that both main
and detached Tab delegates receive the configured icon. The six-item icon menu
opens, changes the selected field value and resets to the terminal icon for a new
Profile. Evidence:
`build/msvc-dynamic-release/test-data/window-restore-3afcafbcea8042ecacb4ef6bbc7b53f0`.
Both processes exited successfully with no direct children. The first picker
probe looked for dynamic menu entries through QObject ancestry and failed; using
the Menu item interface corrected that probe, not product selection behavior.
Dynamic Release, QML formatting/lint, translations (2,404 entries), profile/domain
static analysis and code-health gates pass. This verifies real component wiring,
not an exhaustive physical mouse/keyboard or pixel-level appearance review.

Native drag follow-up: when Windows denied programmatic activation, the smoke
now clicks only after confirming the pointer hits its own window, then rechecks
foreground ownership before dragging. Reordering, Escape cancellation and the
two-detached-window round trip passed in
`build/msvc-dynamic-release/test-data/native-drag-activation-bc31a407d982471998f5a9106e2f3d4d`
(exit 0, no direct children). This supersedes the activation prerequisite above.

The added non-active-Pane drop regression is still **failing**. In the latest
`native-pane-target-*` evidence, preflight resolves the correct main-window leaf,
but the coordinator target is empty before mouse release; the source remains
in its original window and the main layout remains two panes. The failing check
is retained, not waived. An experimental pointer grab-permission change did not
help and was reverted. This checkpoint is not a release-ready drag acceptance;
diagnosing event delivery/target loss was the next goal task.

Follow-up resolves that regression: the detached strip enabled Flickable mouse
dragging only when tabs overflowed. With three tabs in a 320px strip, it consumed
the gesture before the Tab DragHandler activated. The same native gesture passed
when Flickable interaction was disabled. Detached tabs now match the main strip:
mouse dragging belongs to tab reorder/drop; a WheelHandler retains bounded wheel
and touchpad-axis scrolling. The final native check passed, including dropping
into the non-active main-window pane, keeping its sibling and original session
identities, Escape cancellation and the detached-window round trip:
`build/msvc-dynamic-release/test-data/native-drag-events-9f0008d21e6545489f8e4190148cc567`
(exit 0, no direct children left). Dynamic Release, 93-file QML checks and focused
clang-tidy passed. Temporary event/hit-test logs were removed. The input helper
also waits out the system double-click interval after an activation click, so
the following drag cannot accidentally invoke tab rename; this alone did not
fix the overflow failure. Wheel/trackpad hardware scrolling, drag-edge overflow
navigation and mixed-monitor gestures still require separate acceptance.

Detached Pane zoom no longer has an empty signal handler. Main and detached
windows share the workspace-keyed zoom map and leaf lookup, so changing the
visible subtree does not mutate the stored split topology. The real-QML smoke
splits a detached Tab, requests zoom through its viewport signal, switches to a
different Tab and back, then restores the split. It verifies the chosen leaf,
other-Tab isolation, retained zoom, exact restored layout and unchanged main
layout. All five assertions passed in
`build/msvc-dynamic-release/test-data/native-drag-events-7b974e0258cd432bbda686718633f60d`
(exit 0, no direct children). This is component/layout evidence, not a physical
toolbar click or exhaustive keyboard-focus check. Dynamic Release and QML gates
passed; no persistence schema or user-visible strings changed.

Terminal search now uses one `TerminalSearchBar` in each window instead of
opening the main-window search UI from a detached Pane. The component is bound
to its workspace and tracks the active Pane; delayed searches are canceled on
window deactivation or workspace/Pane changes, and execution rechecks ownership.
Real-QML evidence explicitly edits the detached field, switches sessions before
the 250ms debounce, and checks that a main-session sentinel query is unchanged.
Detached search/close and main-window reuse then pass; main search UI remains
closed when opening the detached search. Evidence:
`build/msvc-dynamic-release/test-data/native-drag-events-8c2eb93366e8475ba09b4b772db24b06`
(exit 0, no direct children). Dynamic Release, 94-file QML checks, 2,404-entry
translations, focused clang-tidy and code-health checks pass. Shortcut routing,
physical keyboard/focus and visual search-result acceptance remain separate;
this does not claim all detached-window commands are now window-local.

Detached search shortcut follow-up: each detached window now binds the existing
`terminal.find` action's configured sequence using window-local shortcut scope.
Real Win32 keyboard input verifies default Ctrl+Shift+F opens and closes only the
detached search, then changing the configured sequence to F8 takes effect without
recreating the window. The isolated fixture resets that override afterwards.
Evidence: `build/msvc-dynamic-release/test-data/native-drag-events-6c32532dfb3e49a7a8fe50b48fa8374a`
(exit 0, no direct children). Input is guarded by native foreground ownership
and refuses already-held keys. Dynamic Release, QML and focused static analysis
pass. This closes search-key routing only; other detached commands, overflow
navigation, broad focus/visual and mixed-monitor acceptance remain outstanding.

The suspected detached "browse hosts" reattachment was stale wiring, not a
currently reachable control: `TerminalSessionStateOverlay` no longer emitted
that signal after its browse button was removed. The unused overlay/split-node
signal chain and main/detached handlers are now removed. The live SFTP browse
action remains intact. Repository-wide source checks found no other emitters;
Dynamic Release, QML and translation checks pass. No artificial runtime test
was added to invoke a user-inaccessible signal and call it a product regression.

Kitty unsupported-operation audit found that the pinned engine drops `i`/`I`
when parsing animation commands, then emits an unimplemented error with no
identity. Its encoder discards that empty response. A fail-closed dependency
patch now retains response identity and rejects animation upload/control/compose
with `ENOTSUP`, honoring quiet modes; this does not implement animation playback.
The regression failed before the patch on missing identity, then passed after
it. It covers all three actions, ID/number identity, all three quiet modes,
unchanged shared static pixels and normal image/text recovery. Six related
engine cases passed (eight including setup/cleanup, 820ms); inflation admission,
failed multipart release, Kitty/Sixel separation and metadata bounds also pass.
Cross-session budgets, queue pressure, performance-mode policy and external
client interoperability remain open; these tests do not establish a process-wide
memory ceiling.

The existing active goal remains authoritative; this checkpoint does not mark
the broader terminal/window work complete.

Image renderer audit (2026-09-27): local and SSH presentation coalesce pending
snapshots before queued GUI delivery. AppController uses ordinary connections
to deliver the current snapshot to the viewport; TerminalItem replaces its
snapshot rather than retaining a history. Image overlays are removed when empty.
Qt 6.8.3 `QSGSimpleTextureNode::setTexture` deletes the previous texture when
ownership is enabled, as it is here (verified against the
[upstream 6.8.3 source](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/util/qsgsimpletexturenode.cpp)).
These are ownership findings, not proof of a process-wide memory cap
or a substitute for slow-GUI and multi-session stress measurements.

The audit did identify repeated gray-alpha expansion for shared placements.
An isolated painter probe uses one 1024x1024 gray-alpha raster and a 256x128
output, five paints each with 1 and 32 placements. Before optimization it measured
9.6648 / 307.477 ms; a per-paint single-entry pixel view measured
9.8277 / 10.6304 ms under the same dynamic Release/software setup. The 32-placement
case is about 29x faster in this conversion-heavy probe; this is not an overall
terminal frame-rate claim. Evidence directories are
`build/msvc-dynamic-release/test-data/image-cost-da90beae2d48418c83d96dc5a20a677c`
and `build/msvc-dynamic-release/test-data/image-cost-b328c9843b724150b9241957936258e9`.
Both full-app image checks exited 0, preserving 11,520 red Sixel pixels,
1,024 blue Kitty pixels and 4,096 green placeholder pixels, with no direct
children remaining. The cache lives only inside a paint call and retains at
most one conversion; nonconsecutive differing images still incur conversion.
No global cache or additional persistent pixel ownership was introduced.
The final rerun also checks white/black/white sources of identical dimensions,
preventing accidental reuse across different rasters. It passed with
9.8652 / 10.6269 ms and the same protocol pixel counts in
`build/msvc-dynamic-release/test-data/image-cost-37225a24073e4ce286451920fe25df2d`.
Dynamic Release, focused clang-tidy, formatting and code-health checks passed.

Slow-GUI image delivery now has a focused local-session regression. It feeds
80 replacements of a 128x128 RGB Kitty raster through the production output
consumer on a producer thread while deliberately not pumping GUI events. All
5,327,717 input bytes finish processing; only one pending snapshot is built.
After GUI processing resumes, two deliveries recover the last raster and final
text, and a weak reference confirms release of the original snapshot pixels.
The fixture retains only its latest snapshot, not a QSignalSpy history. It does
not start a shell or exercise ConPTY, GPU upload or SSH transport, and does not
measure a process memory ceiling. Evidence:
`build/msvc-dynamic-release/image-stall-tests.txt` (three cases plus setup/cleanup,
five passes, 1,778ms). The two adjacent cases check synchronized-output timeout
recovery and selection interrupting synchronized output. The initial fixture
lacked cell pixel dimensions and therefore had no visible image placements;
adding realistic 8x16 cell geometry fixed the fixture, not production behavior.

Image/effects policy acceptance (2026-09-27): the existing full-app image smoke
now changes the persisted effects tier through the controller (full/reduced/off)
and captures each result. Normal mode reports text-blink eligibility true/false/
false; a separately booted performance-mode fixture reports false for all three
saved tiers. All six captures preserve the same red/blue/green image pixel counts
(11,520 / 1,024 / 4,096). Both runs exit 0 with no direct children remaining:
`build/msvc-dynamic-release/test-data/image-cost-0e7ce631e581418797e2fb6fb53786ab`
and `build/msvc-dynamic-release/test-data/image-policy-performance-1790474495942`.
This verifies QML motion-policy binding and content preservation on the software
backend, not SGR glyph phase/timer behavior or native GPU memory. Isolated test
preferences are restored after a successful run; real user settings are untouched.

Detached close-selection audit found a real controller bug: explicit successor
IDs were honored only for main-window workspaces. Closing the last-position
active Tab of a detached group could select a main session while the detached UI
displayed its surviving Tab. `closingDetachedTabKeepsItsWindowSuccessor` fails
before the fix and passes after honoring any existing explicit successor.
The detached close action now chooses the next Tab, or the previous one at the
right edge, rather than the first unrelated Tab in its group. Closing an inactive
Tab still leaves active context untouched. Three focused controller cases pass
(five including setup/cleanup, 392ms); no full regression was run.

The real-QML multi-window check now asserts the active controller session and
displayed detached successor immediately after close, before any manual
selection can mask divergence. With A/copy/B it selects B and preserves the main
selection. The first UI assertion incorrectly expected A after a preceding
drag round trip; explicit ID diagnostics identified that fixture expectation,
which was corrected to the actual right-hand neighbor, not relaxed to any Tab.
Final evidence:
`build/msvc-dynamic-release/test-data/native-drag-events-7252165f80a14aea8d8fcf644d0726aa`
(exit 0, no direct children). Native exact-Pane drag, search keys, zoom, rename,
duplication and caption-state round trip also pass in that run. Dynamic Release,
94-file QML checks, focused clang-tidy, formatting and code-health checks pass.
The close itself is invoked through the QML handler, not a physical close-button
click; other shortcut/overflow and mixed-monitor checks remain open.

Detached keyboard routing follow-up: the main action repeater uses window-local
shortcut scope, so independent windows did not inherit its bindings. Detached
windows now register their own configured sequences for search, next/previous
Tab, close, split/duplicate Pane, Pane focus, resize and swap. Dispatch first
activates that window's selected workspace; it does not call the main-window
presentation switch. Close follows existing Pane semantics (close the active
Pane, or use same-window Tab succession when only one remains). Rename dialogs
disable these bindings. No new default sequences or translated labels were added.

Native-key evidence verifies Ctrl+Tab / Ctrl+Shift+Tab stay inside the detached
group, Alt+Shift+H adds a Pane there, and Ctrl+Shift+W removes it without changing
the main layout or selection. Search default/rebound keys, exact-Pane drag and
close succession still pass. Evidence:
`build/msvc-dynamic-release/test-data/native-drag-events-e518e79669ef45e5bc1bff4739cc68c4`
(exit 0, no direct children). Dynamic Release, 94-file QML, 2,404 translations,
format and code-health checks pass. This is not yet a claim that every application
action (workbench/composer/palette/new Tab) has detached-window presentation, nor
that the newly registered focus/resize/swap keys have physical-input acceptance.

New local Tab and duplicate actions now use the detached window's own insertion
path (including its existing duplicate context-menu action). The temporary main
ownership during session creation no longer replaces the remembered main-window
selection. Native Ctrl+Shift+T creates/selects a Tab in the detached group, and
Ctrl+Shift+W closes it there. The fixture deliberately leaves two main Tabs with
the non-first one selected; that selection survives both actions. Evidence:
`build/msvc-dynamic-release/test-data/native-drag-events-12a0307875ba4cf3aef650a2448e40e0`
(exit 0, no direct children). Existing duplication, switching, split, search and
drag checks pass in the same run. Dynamic Release/QML, translation, formatting
and code-health checks pass; palette/workbench presentation remains separate.

1. Exact-Pane native mouse transfer, search-key routing, local focus/resize/swap,
   new/close actions and native Snap hover now pass. Do not infer every Snap tile
   selection from the hover screenshot or all auxiliary-window actions from the
   terminal-specific action checks.
2. Physical mixed-display/hot-unplug acceptance awaits suitable hardware. Per-window selection,
   geometry/maximization persistence, startup absent-screen recovery and restore
   switches are implemented with focused controller/store and native evidence.
3. Profile icon captures and light/dark main/detached progress/notification
   presentation now pass; routing/focus checks are separate from protocol tests.
4. Kitty/Sixel static interoperability, unsupported-animation rejection,
   resource limits, shared storage budgeting and focused performance evidence
   are recorded above. This is not full Kitty protocol conformance.
5. Reconcile translations, ADRs and this status document; run only the affected
   module/integration and runtime checks. Report remaining manual acceptance
   explicitly instead of treating unit tests as native UI acceptance.

### Window-interaction closeout (2026-09-27)

This status supersedes historical follow-up notes above, without replacing their
individual workload evidence. The owner confirmed real-pointer hover over an
inactive detached Tab, switching its view and merging a dragged Pane into it.
This acceptance is user-reported, not a newly completed automation run.

The old Pane title component/resource, main per-workspace title-visibility map,
recursive title layout branches, legacy detached-Pane toolbar mode and window-
movement auto-docking signal chain are removed. Main Pane handles focus/drag;
detached handles toggle the Tab bar, moving the window while that bar is hidden.
Explicit reattach-all is the whole-window return action. Ordinary blank-caption
window movement never implicitly transfers the selected Tab.

PowerShell's post-startup executable-path title follows the existing terminal
title policy (manual override, allowed terminal title, then Profile/default).
No path-title normalization is planned in this closeout. Clearing a manual title
restores automatic policy; disabling terminal title changes retains the default.

Tray exit now captures placements before closing windows and bypasses ordinary
detached-Tab deletion/veto. The two-startup live-session regression verifies exit
from visible and tray-hidden main windows, preserved topology and no child
processes; see ADR 0036. Physical mixed-DPI/hot-unplug acceptance remains pending.
Installer/Sandbox release acceptance is tracked separately in ADR 0134.
