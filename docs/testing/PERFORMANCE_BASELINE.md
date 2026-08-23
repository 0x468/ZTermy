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
in timing collection.

## Repetition

1. Close unrelated high-load applications and keep the power plan unchanged.
2. Record five warm runs for each configuration.
3. Archive each JSON and log under a machine/configuration-specific folder outside the build directory before the next run.
4. Compare medians; inspect P95/P99/max for stutter and outliers.
5. Do not compare Debug and Release results.

## Required configurations

| Configuration | Purpose |
|---|---|
| D3D11 default, current backdrop | Product baseline. |
| D3D11 default, Acrylic | Backdrop composition comparison. |
| D3D11 default, Mica | Lower-motion material comparison. |
| D3D11 default, true opaque diagnostic mode | Isolate alpha/DWM composition cost once implemented. |
| Requested WARP | Expose software-renderer failure behavior; not a product target. |
| DPR 1.0 and 1.5/2.0 where available | Quantify physical-pixel scaling. |

Request WARP only for the diagnostic comparison:

```powershell
$env:QSG_RHI_PREFER_SOFTWARE_RENDERER = "1"
cmake --build --preset msvc-static-release --target ztermy_performance_baseline
Remove-Item Env:QSG_RHI_PREFER_SOFTWARE_RENDERER
```

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
