# ztermy performance program

## Objective

Make ztermy predictably responsive on Windows 11 hardware ranging from current desktop GPUs to older integrated GPUs.
Performance work is evidence-driven: no renderer rewrite, material downgrade, or QML simplification is accepted without a
repeatable before/after comparison.

## Product performance contract

- Preserve terminal correctness, IME, ligatures, wide characters, selection, transparency, Snap Layouts, and accessibility.
- Measure static Release builds for product decisions. Debug builds are diagnostic only.
- Compare the same executable preset, window size, DPI, backdrop, terminal palette, font, and workload.
- Record at least five warm runs; use the median for comparisons and retain P95/P99/max values for stutter analysis.
- A change must improve its target metric without materially regressing input latency, correctness, memory, or idle power.
- A result with an unexposed window, fewer than 30 render samples, a software backend that was not requested, or missing
  environment metadata is invalid rather than “fast”.

## Reference findings

The program is based on primary documentation and public upstream implementations:

- Qt's [Qt Quick performance guide](https://doc.qt.io/qt-6/qtquick-performance.html) identifies frame budget, binding,
  clipping, transparency, and animation costs.
- Qt's [scene graph documentation](https://doc.qt.io/QT-6/qtquick-visualcanvas-scenegraph.html) recommends retained scene
  graph nodes over a two-step software rasterize-then-texture path for performance-sensitive custom items.
- Qt documents `QSG_INFO`, `QSG_RENDER_TIMING`, batching diagnostics, D3D11 as the Windows default, and possible WARP
  fallback in its [default renderer](https://doc.qt.io/qt-6.8/qtquick-visualcanvas-scenegraph-renderer.html) and
  [Windows graphics](https://doc.qt.io/qt-6.8/windows-graphics.html) references.
- Ghostty's public [`RenderState`](https://github.com/ghostty-org/ghostty/blob/main/src/terminal/render.zig) retains memory,
  consumes dirty state, and separates the terminal-locked update phase from deferred renderer work.
- Windows Terminal separates terminal buffer interpretation from renderer engines in its
  [repository organization](https://github.com/microsoft/terminal/blob/main/doc/ORGANIZATION.md); its Atlas renderer work
  is a reference for retained glyph/attribute data and GPU-oriented text rendering, not code to copy.

## Current hypotheses

| ID | Hypothesis | Evidence required | First experiment |
|---|---|---|---|
| H1 | Full `QImage` rasterization and texture recreation dominate active terminal frames. | Paint and texture P95/P99, uploaded bytes, rendered frames. | Current renderer baseline; then persistent backing store/row damage A/B. |
| H2 | Cursor blink causes avoidable full-surface uploads while idle. | Cursor invalidations and uploaded bytes during an idle interval. | Separate cursor node A/B after baseline. |
| H3 | Alpha-window composition and Acrylic amplify resize/move stalls on integrated GPUs. | Identical workload across solid, Mica, Acrylic, and transparent modes. | Add a genuine opaque diagnostic path before changing defaults. |
| H4 | QML relayout and delegate churn dominate Hosts, Settings, and AI workloads. | QML Profiler traces plus GUI event-loop and frame pacing histograms. | Profile representative large-data scenarios before lazy loading/virtualization. |
| H5 | Backend or driver selection explains machine-specific regressions. | Actual RHI backend, adapter log, WARP flag, OS, DPI. | Capture `QSG_INFO=1`; compare D3D11 hardware and requested WARP. |

## Measurement layers

1. **Environment**: version, build type, Qt version, OS, CPU architecture, RHI backend, adapter log, WARP request, DPI,
   window size, alpha buffer, backdrop, and terminal opacity.
2. **Terminal producer**: bytes read, snapshots produced/delivered/coalesced, full/partial/clean damage, snapshot build time.
3. **Terminal renderer**: paint and texture-creation latency histograms, rendered damage, snapshot updates, cursor
   invalidations, maximum pixels, and estimated upload bytes.
4. **UI responsiveness**: frame swaps, completion time, event-loop heartbeat gaps, resize completion, and functional checks.
5. **System observation**: GPU engine, dedicated/shared memory, CPU per core, working set, and power behavior, captured with
   Windows tooling when investigating a specific machine.

## Work stages

### P0 — reproducible evidence

- Release-capable opt-in terminal metrics.
- `ztermy_performance_baseline` target and versioned JSON schema.
- Strict evidence validation and like-for-like before/after report generation.
- Reject hidden/headless runs rather than accepting misleading frame counts.
- Document the desktop, low-end, high-DPI, and WARP matrix.

### P1 — low-risk renderer corrections

- Coalesce snapshots to display cadence without delaying input.
- Stop cursor blink from rebuilding terminal content.
- Reuse allocations and cached keyword/layout work where measurements justify it.

### P2 — damage-aware rendering

- Retain a backing image/texture across frames.
- Redraw only changed rows plus selection/cursor invalidation regions.
- Verify scroll, resize, IME, wide text, ligatures, and transparency against the full-redraw reference.

### P3 — native retained renderer decision

Only if P2 misses the agreed budgets, prototype a glyph-atlas and geometry/QRhi renderer behind an A/B switch. Decide from
measured benefit, implementation risk, text fidelity, and maintenance cost. Replacing the whole QML shell with QWidget is
not part of this program.

### P4 — QML and composition hotspots

- Profile Hosts, Settings, SFTP, telemetry, and AI streaming independently.
- Virtualize or lazy-load only measured hotspots.
- Prefer transform/opacity animation over layout-property animation where it changes frame cost.
- Compare true opaque, Mica, Acrylic, and transparent modes on low-end hardware.

## Provisional budgets

These are investigation thresholds, not release gates until the first low-end baseline is recorded:

- Interactive desktop: no sustained GUI stall above 100 ms; P95 event-loop gap below 33 ms during output.
- Terminal viewport at 1120×800, DPR 1.0: paint P95 below 8 ms and paint+upload P95 below 12 ms.
- Idle blinking cursor: zero full terminal texture uploads after cursor separation.
- Output burst: bounded snapshot backlog and no increasing latency trend during a 30-second soak.
- Low-end fallback: maintain at least a stable 30 Hz interaction path without silently using an invalid benchmark result.

Budgets will be revised from captured desktop and low-end evidence, with the revision recorded in this document.
