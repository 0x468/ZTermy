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
