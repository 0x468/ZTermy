# Performance optimization results

This log records accepted and rejected performance experiments. Results are evidence from the interactive Windows desktop,
not headless or Debug runs.

## Environment

- ztermy 0.3.0 static Release
- Qt 6.8.3 / MSVC 2022
- Windows 11 25H2
- Direct3D 11 scene graph
- 1120 x 800 logical window, DPR 1.0
- Acrylic backdrop, terminal background opacity 1.0

## Baseline workload

The automated workload writes 20,000 PowerShell lines, resizes from 1120 x 800 to 780 x 520 and back, searches for a
completion marker, traverses scrollback, and then observes 2.2 seconds of idle cursor blinking. Reports are accepted only
when all functional checks pass and the strict evidence validator accepts the sample counts and graphics backend.

## Experiment 1: opaque texture hint

Result: rejected and reverted.

| Metric | Baseline | Candidate |
|---|---:|---:|
| Paint P95 | 4,000 us | 4,000 us |
| Texture creation P95 | 50 us | 50 us |
| Completion | 1,727 ms | 1,729 ms |
| Maximum heartbeat gap | 13 ms | 17 ms |

The Qt-documented opaque hint may reduce blending on some systems, but this workload did not prove a gain and recorded a
worse heartbeat outlier.

## Experiment 2: cached cursor row

Result: superseded by experiment 3.

| Metric | Baseline | Candidate | Change |
|---|---:|---:|---:|
| Idle paint P95 | 2,000 us | 250 us | -87.5% |
| Idle paint maximum | 1,897 us | 124 us | -93.5% |
| Idle texture upload | 13,070,592 bytes | 13,070,592 bytes | 0% |

Repainting only the cursor row proved that CPU raster work was removable, but replacing the full texture still uploaded the
same amount of data.

## Experiment 3: separate cursor node

Result: accepted.

| Metric | Baseline | Candidate | Change |
|---|---:|---:|---:|
| Output/resize paint P95 | 4,000 us | 4,000 us | 0% |
| Output/resize heartbeat maximum | 16 ms | 16 ms | 0% |
| Idle paint P95 | 2,000 us | 50 us | -97.5% |
| Idle paint maximum | 1,897 us | 6 us | -99.7% |
| Idle texture upload | 13,070,592 bytes | 0 bytes | -100% |

The terminal surface remains one custom item. Only the cursor is a small scene-graph child node, so blinking changes node
geometry without repainting or replacing the base texture.

## Remaining hotspot

The high-throughput phase still performs mostly full damage: after delivery pacing, the median run rendered 162 frames and
replaced roughly 455 MiB of terminal texture data while processing 20,000 lines. This is now isolated from idle performance
and remains a future optimization candidate. Any change must preserve terminal rendering, resize, scrollback, IME,
selection, and transparency behavior and must beat the same Release workload.

## Experiment 4: pace latest snapshot delivery

Result: accepted after five warm baseline and five warm candidate runs.

| Median metric | Immediate delivery | 8 ms latest delivery | Change |
|---|---:|---:|---:|
| Completion | 1,728 ms | 1,729 ms | +0.1% |
| Paint P95 | 4,000 us | 4,000 us | 0% |
| Paint maximum | 7,701 us | 7,356 us | -4.5% |
| GUI snapshot updates | 1,647 | 159 | -90.3% |
| Rendered frames | 250 | 162 | -35.2% |
| Estimated texture upload | 752,572,864 bytes | 476,669,312 bytes | -36.7% |

The worker still parses all bytes and keeps replacing the pending snapshot. The GUI receives only the newest state once per
8 ms interval, which removes work that could not become a distinct displayed frame.

## Experiment 5: lazy-retained terminal workbench

Result: accepted after five warm eager baselines and repeated lazy-retained candidates.

The baseline created the complete terminal workbench, including the AI conversation subtree, before it was ever opened.
The candidate uses a `Loader`: the subtree is absent until first use, then remains retained and hidden so later switching
does not recreate it.

| Median metric | Eager workbench | Lazy-retained workbench | Change |
|---|---:|---:|---:|
| QML load | 469 ms | 418 ms | -10.9% |
| Closed-state `QObject` count | 10,166 | 7,139 | -29.8% |
| Closed-state `QQuickItem` count | 4,432 | 3,110 | -29.8% |
| First workbench open | 48 ms | 93 ms | +45 ms |
| Reopen after close | not measured | 20 ms | new metric |
| 240-chunk stream duration | 2,588 ms | 2,601 ms | +0.5% |
| Maximum GUI heartbeat gap | 146 ms | 136 ms | -6.8% |
| Presented frames | 104 | 262 | diagnostic only |

The change removes roughly three thousand UI objects from every startup and does not materially regress the fixed stream
duration. The one-time 45 ms first-open cost is below 100 ms and subsequent reopening is about 20 ms. Presented-frame
count increased while the instantiated workbench tree was otherwise equivalent; this is consistent with more progressive
presentation, but is not treated as an independent improvement without GPU/CPU evidence.

An unload-on-close variant reduced the same startup object count and opened in roughly 62 ms, but paid creation cost on
every reopen. It was rejected because the retained variant better matches repeated terminal-tool use while keeping the
startup gain.

## Experiment 6: window composition matrix and WARP fallback

Result: measurement infrastructure accepted; no product material change justified on the reference desktop.

The same terminal and UI workloads were run five times in interleaved order with Qt's shader disk cache disabled. The
reference workload reports zero milliseconds of runtime pipeline creation, so disabling the unused cache provides cleaner
isolation than attempting to warm a file Qt does not generate. The report validated the actual DWM backdrop and
alpha-buffer state before accepting a sample. Values below are medians; upload volume is the renderer's estimated
full-surface texture traffic.

| Composition path | Alpha bits | DWM type | Terminal completion | Terminal max gap | Upload | QML load | First AI open | AI stream | UI max gap |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Acrylic | 8 | 3 | 1,831 ms | 17 ms | 480 MiB | 406 ms | 82 ms | 2,638 ms | 142 ms |
| Mica | 8 | 2 | 1,737 ms | 16 ms | 459 MiB | 409 ms | 82 ms | 2,621 ms | 142 ms |
| Mica Alt | 8 | 4 | 1,820 ms | 17 ms | 475 MiB | 404 ms | 87 ms | 2,706 ms | 150 ms |
| Transparent | 8 | 1 | 1,836 ms | 16 ms | 492 MiB | 408 ms | 87 ms | 2,618 ms | 142 ms |
| True opaque diagnostic | none | 1 | 1,733 ms | 17 ms | 469 MiB | 408 ms | 88 ms | 2,641 ms | 141 ms |

No composition path has a consistent cross-metric advantage: the true opaque surface has the shortest terminal median in
this run, but its first-workbench-open median is slower than Acrylic and its upload and heartbeat metrics remain in the
same range. The variation is comparable to the spread between raw runs, so the data rejects an automatic backdrop
downgrade or a new opaque product mode. Low-end physical hardware remains a separate validation axis.

An explicitly requested D3D11 WARP run selected Microsoft Basic Render Driver and completed both scenarios. The terminal
workload completed in 2,027 ms with a 21 ms maximum gap, 266 frames, 4 ms paint P95, and 543 MiB estimated upload; idle
cursor upload remained zero. The UI workload loaded in 393 ms, first-opened in 88 ms, streamed in 2,715 ms, and recorded a
140 ms maximum gap. This single run demonstrates functional software-renderer degradation; it is not used as a hardware
performance comparison.

## Diagnostic incident: stale incremental object layout

During the UI benchmark, an existing static Release build intermittently exited with heap corruption. Full PageHeap
converted the symptom into a deterministic access violation in `LocalTerminalSession` construction. Symbol and
disassembly inspection showed constructor code writing members beyond the allocation size: the executable mixed objects
compiled before and after the snapshot-pacing class layout changed.

This was an invalid measurement environment, not a candidate regression. A complete clean rebuild removed the mismatch;
five PageHeap launches and five normal AI UI benchmark launches then exited successfully. Touching the header after the
clean build rebuilt its implementation, generated MOC source, `AppController`, and `main`, confirming the current Ninja
dependency graph tracks the header. No speculative build-system workaround was retained.

## Current conclusion

The first performance pass found two independent, measured costs rather than a single “QML is slow” cause:

1. terminal output generated GUI snapshots and full texture uploads faster than the display could present them; pacing
   removed 90.3% of GUI snapshot deliveries and 36.7% of estimated upload volume without changing completion time;
2. the unopened workbench eagerly created about 30% of the startup QML object tree; lazy retention removes that startup
   work at a measured 45 ms one-time first-open cost;
3. desktop composition material is not a measurable bottleneck in the controlled matrix, while explicitly requested WARP
   remains functional with modest degradation.

Low-end physical composition, resizing, complex Markdown, and full terminal damage remain separate diagnostic axes.
Further changes require a new comparable baseline rather than extrapolating from these results.
