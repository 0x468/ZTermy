# Performance baseline procedure

## Build

Use the static Release preset for decisions:

```powershell
cmake --preset msvc-static-release
cmake --build --preset msvc-static-release --target ztermy_performance_baseline
```

The target opens a visible ztermy window, exercises a fixed 20,000-line local PowerShell workload, resizes the window, and
writes:

```text
build/msvc-static-release/test-data/performance-baseline/terminal-performance.json
build/msvc-static-release/test-data/performance-baseline/logs/ztermy.log
build/msvc-static-release/test-data/performance-baseline/terminal-render-complete.png
```

Run it from an unlocked interactive Windows desktop. A service session, hidden desktop, minimized window, remote runner
without GPU presentation, or an occluded automation environment is not valid evidence. The command intentionally fails if
the window is not exposed.

The target uses `ztermy_performance_launcher.exe`, a build-only Windows GUI launcher that supplies the fixed benchmark
arguments from the same interactive desktop. It is not installed or included in release packages and does not participate
in timing collection. The CMake targets disable Qt's shader disk cache so unrelated user-profile cache state cannot
contaminate a comparison or cause concurrent cache lock warnings. The reference workload reports zero milliseconds of
runtime pipeline creation, so a synthetic warm-cache pass would add noise without warming anything.

## Repetition

1. Close unrelated high-load applications and keep the power plan unchanged.
2. Record five warm runs for each configuration.
3. Archive each JSON and log under a machine/configuration-specific folder outside the build directory before the next run.
4. Compare medians; inspect P95/P99/max for stutter and outliers.
5. Do not compare Debug and Release results.

## QML and AI workbench scenario

Use the companion UI benchmark to measure startup object creation, first-open cost, retained workbench cost, AI Markdown
streaming responsiveness, and reopen latency:

```powershell
cmake --build --preset msvc-static-release --target ztermy_ui_performance_baseline
```

It opens a visible 1120 x 800 D3D11 window, starts a local terminal, opens the AI workbench, streams 240 representative
Markdown chunks at 8 ms intervals, closes and reopens the workbench, and writes:

```text
build/msvc-static-release/test-data/performance-ui-baseline/ui-performance.json
build/msvc-static-release/test-data/performance-ui-baseline/logs/ztermy.log
build/msvc-static-release/test-data/performance-ui-baseline/ui-performance.png
```

The JSON records QML load and local-terminal-ready latency; `QObject` and `QQuickItem` counts before first workbench use
and after creation; first-open and reopen latency; stream duration; GUI heartbeat gaps; and presented frame count. Treat
frame count as diagnostic evidence rather than a score: more frames can mean smoother progressive presentation, but can
also mean more render work.

For focused diagnostics only, set `ZTERMY_UI_BENCHMARK_PAGE` to another workbench page, set
`ZTERMY_UI_BENCHMARK_CHUNKS` to `0` through `2000`, or set `ZTERMY_UI_BENCHMARK_CAPTURE=0` to omit the screenshot. Do not
mix different values in a before/after comparison.

The benchmark enables `QSG_INFO=1` and `QSG_RENDER_TIMING=1`. Qt documents the latter as a scene-graph bottleneck aid and
recommends checking that rendering is actually the bottleneck before changing renderer architecture. For deeper
diagnosis, use `QSG_RENDERER_DEBUG=render`, `QSG_VISUALIZE=batches`, `QSG_VISUALIZE=changes`, or
`QSG_VISUALIZE=overdraw` one at a time; these visualization modes are not timing-comparable product runs.

## Composition matrix

Run the fixed terminal and UI workloads five times for Acrylic, Mica, Mica Alt, transparent, and a genuine opaque
diagnostic surface:

```powershell
cmake --build --preset msvc-static-release --target ztermy_composition_performance_matrix
```

The target disables the shader disk cache, collects the configurations in interleaved order, validates the requested DWM
backdrop and alpha-buffer state, then writes the median table and raw-run appendix to:

```text
build/msvc-static-release/test-data/composition-performance-matrix.md
```

The opaque case disables the window alpha buffer before `QGuiApplication` construction and requests no system backdrop;
it is therefore an actual composition diagnostic rather than a transparent surface painted with opaque content. This
mode is benchmark-only and does not change the product's appearance choices.

## Focused terminal diagnostics

Two opt-in targets explain active terminal paint without changing normal product behavior:

```powershell
cmake --build --preset msvc-static-release --target ztermy_terminal_row_reuse_diagnostic
cmake --build --preset msvc-static-release --target ztermy_terminal_paint_phase_diagnostic
```

The first writes `test-data/performance-row-reuse/terminal-performance.json` and reports exact rows that a hypothetical CPU
backing store could preserve. The second writes `test-data/performance-paint-phases/terminal-performance.json` and splits
paint time into image preparation, snapshot/keyword preparation, background cells, text, and cursor/IME overlay. Both are
diagnostics: run five warm samples before making a product decision, and do not compare their completion time directly
against a baseline that did not pay the diagnostic bookkeeping cost.

## Required configurations

| Configuration | Purpose |
|---|---|
| D3D11 default, current backdrop | Product baseline. |
| D3D11 default, Acrylic | Backdrop composition comparison. |
| D3D11 default, Mica | Lower-motion material comparison. |
| D3D11 default, true opaque diagnostic mode | Isolate alpha/DWM composition cost once implemented. |
| Requested WARP | Expose software-renderer failure behavior; not a product target. |
| DPR 1.0 and 1.5/2.0 where available | Quantify physical-pixel scaling. |

Request WARP only for the diagnostic comparison. The dedicated target disables the shared shader disk cache and runs
both workloads with the software-renderer preference:

```powershell
cmake --build --preset msvc-static-release --target ztermy_warp_performance_diagnostic
```

The run is valid only when `QSG_INFO` identifies Microsoft Basic Render Driver; WARP is a correctness and responsiveness
fallback diagnostic, not a product performance target.

## Validity checks

- `environment.buildType` is `release`.
- `environment.graphicsApi` matches the intended backend.
- `terminalRenderer.paint.samples` and `terminalRenderer.textureCreate.samples` are at least 30.
- `scenario.frameSwaps` is at least 30.
- The log identifies the selected adapter and does not unexpectedly select Microsoft Basic Render Driver.
- The screenshot contains rendered terminal content and the scenario reports successful completion and resize.

If a check fails, label the run invalid and fix the measurement environment before drawing conclusions.

The build includes a strict validator that applies these checks:

```powershell
./build/msvc-static-release/ztermy_performance_report.exe --validate `
  ./build/msvc-static-release/test-data/performance-baseline/terminal-performance.json
```

It returns a non-zero exit code for Debug reports, incomplete scenarios, fewer than 30 active frame/timing samples, fewer
than three idle cursor samples, or an unusable graphics backend. The report schema includes a 2.2-second idle cursor phase
so terminal output performance and idle repaint cost can be compared independently.

## Before/after comparison

Archive a valid baseline before making an optimization, capture the candidate with identical settings, then run:

```powershell
./build/msvc-static-release/ztermy_performance_report.exe --compare `
  ./evidence/baseline.json `
  ./evidence/candidate.json `
  --output ./evidence/comparison.md
```

The comparator refuses different build types, Qt versions, graphics backends, DPI values, window sizes, backdrops,
terminal opacity, or software-renderer preferences. Its Markdown table reports paint, texture creation, GUI heartbeat,
completion, upload-volume, frame, snapshot, and cursor-invalidation deltas.

## Manual low-end observation

During the same run, observe per-engine GPU graphs and per-core CPU usage rather than aggregate CPU alone. Record whether
stutter occurs during output, cursor-only idle, resize, window movement, AI streaming, or material changes; these symptoms
route to different hypotheses in `docs/PERFORMANCE_PROGRAM.md`.

Also test the same workflow once with Mica and once with Acrylic. Microsoft describes Mica as a wallpaper-sampled,
performance-oriented foundation material, while Acrylic performs live translucent composition and is more GPU-intensive.
Material cost therefore remains a separate variable; it is not evidence that QML itself is slow.

## Primary references

- [Qt Quick performance considerations](https://doc.qt.io/qt-6/qtquick-performance.html)
- [Qt 6.8 Loader](https://doc.qt.io/qt-6.8/qml-qtquick-loader.html)
- [Qt Quick scene graph default renderer](https://doc.qt.io/qt-6.8/qtquick-visualcanvas-scenegraph-renderer.html)
- [Qt Quick graphics configuration and pipeline cache](https://doc.qt.io/qt-6/qquickgraphicsconfiguration.html)
- [Qt 6.8 command-line QML profiler](https://doc.qt.io/qt-6.8/qtqml-tooling-qmlprofiler.html)
- [Windows system backdrops](https://learn.microsoft.com/en-us/windows/apps/develop/ui/system-backdrops)
- [Windows Acrylic guidance](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic)
