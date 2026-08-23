# Performance manual acceptance

This checklist is the human-evidence gate for the first data-driven performance pass. Use the static Release executable,
not Debug. Keep the display refresh rate and Windows power mode unchanged for the whole comparison.

## Evidence collector

On the physical low-end Windows 11 machine, run the collector from an unlocked interactive desktop:

```powershell
pwsh -ExecutionPolicy Bypass -File .\scripts\collect_performance_acceptance.ps1 `
  -IncludeCompositionMatrix
```

It records the Windows build, CPU, memory, GPU and driver, current resolution and refresh rate, power scheme,
virtualization state, Windows transparency and client-area-animation preferences, DWM composition, Remote Desktop state,
and Battery Saver. It then runs and validates the static Release terminal/UI benchmarks, optionally performs the full
five-material matrix, and writes an immutable-style bundle with SHA-256 hashes beneath
`build/msvc-static-release/test-data/performance-acceptance/`. Fill in the generated `manual-observations.md`; the JSON and
screenshots prove automated behavior, while the observation sheet records the physical interaction evidence they cannot.
For final acceptance, `environment.json` must record the intended commit and `source.dirty` must be `false`.

Use `-CollectOnly` to verify hardware collection without opening benchmark windows. A collect-only bundle does not satisfy
the performance gate.

## 1. Startup and first workbench use

1. Start ztermy on the Hosts page and do not open a terminal workbench.
2. Open a local or SSH terminal.
3. Open the AI workbench for the first time, close it, then reopen it.

Expected:

- Hosts and the initial terminal appear without a visible blank-window pause.
- The first workbench open may take one short beat (measured below 100 ms on the development machine), but must not freeze
  input or show partially constructed controls.
- Reopening should feel immediate and preserve the existing workbench state.

## 2. High-throughput terminal output

Run a workload that produces tens of thousands of lines, then continue typing while output is active. On PowerShell, the
automated benchmark uses 20,000 fixed lines; any harmless equivalent is acceptable for manual observation.

Expected:

- input remains responsive and output eventually contains the completion marker;
- no progressively increasing delay, stale final frame, missing prompt, or crash;
- resizing during output shows bounded catch-up rather than a permanently black or stale region;
- idle cursor blinking after completion does not cause visible terminal-content repainting.

## 3. AI streaming and interaction

Ask the configured provider for a long Markdown response containing headings, lists, code, and a table. While it streams:

1. move the pointer across text and controls;
2. select a paragraph;
3. scroll upward and remain away from the bottom for several seconds;
4. return to the bottom, expand/collapse reasoning and tool cards, close and reopen the workbench.

Expected:

- pointer shape and text selection do not flicker or reset;
- manual scroll position is preserved while away from the bottom; bottom-follow resumes only after returning to the end;
- code/table horizontal scrolling is local to that block and does not widen the sidebar;
- terminal input/output continues while the AI UI is busy.

## 4. Material isolation

Repeat the same resize, window move, terminal burst, and AI stream once each with Mica and Acrylic at the same opacity.
Observe Task Manager's per-engine GPU graphs and CPU per-core graphs, not only aggregate CPU.

Expected:

- neither material changes correctness or produces multi-second stalls;
- Mica may be cheaper because Windows samples the wallpaper rather than continuously exposing the desktop;
- Acrylic may consume more GPU/power. Record this as a material result, not as a QML regression.

If low-end hardware shows a clear material-specific problem, record OS build, GPU/driver, DPI, refresh rate, backdrop,
window size, and whether Windows transparency/battery saver was enabled.

Windows is allowed to replace Acrylic or Mica with an opaque fallback when transparency is disabled, the session is
remote or virtualized without adequate compositor support, graphics support is insufficient, High Contrast is active,
or (for Acrylic) Battery Saver is active. That is distinct from a rendering defect: record whether the result is a clean
solid fallback or visually corrupted. The release identity card also deliberately follows the Windows client-area
animation preference. If its orbit and sweep are completely stationary, compare the observation with
`visualEnvironment.clientAreaAnimationsEnabled` in `environment.json` before treating it as a frame-rate regression.

The terminal and UI benchmark reports record Qt Quick's selected graphics API. `software` on a run that did not request
software rendering is a diagnostic failure; Direct3D 11 backed by a virtual adapter may still be substantially slower
than a physical GPU and must be identified from `graphicsAdapters`.

## 5. Required evidence to report

For each machine, report:

- CPU, GPU, RAM, Windows version, screen resolution/DPI, refresh rate, graphics driver, virtualization/remote-session
  state, DWM composition, transparency, client-area-animation, and Battery Saver state;
- Mica versus Acrylic subjective result;
- whether startup, first workbench open, terminal output, resize, AI streaming, or idle cursor was the visible bottleneck;
- the generated `terminal-performance.json` and `ui-performance.json` when possible;
- a short screen recording only when it captures a visible stall that the JSON cannot explain.

Pass means all correctness expectations hold and there is no sustained interaction stall above roughly 100 ms. A failure
should be routed to its measured layer before any further optimization is attempted.
