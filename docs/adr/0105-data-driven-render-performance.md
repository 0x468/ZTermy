# ADR 0105: Data-driven render performance work

- Status: Accepted
- Date: 2026-08-23

## Context

ztermy uses a Qt Quick shell and a native `QQuickItem` terminal viewport. The viewport currently paints a complete
device-pixel `QImage` and creates a new scene-graph texture whenever its revision changes. Terminal damage metadata is
available but is not yet used to reduce rasterization or upload work. The top-level window also always requests an alpha
buffer and Windows backdrop composition.

Perceived stutter can originate in the GUI thread, render thread, terminal snapshot producer, GPU upload, DWM composition,
or a software graphics fallback. Aggregate CPU and memory utilization cannot distinguish these causes.

## Decision

Performance changes will follow a measurement-first program:

1. Collect opt-in metrics in Release builds with negligible disabled cost.
2. Persist a versioned JSON record containing environment, scenario, functional outcome, and latency distributions.
3. Reject non-representative runs, including windows that never become exposed and undersampled render data.
4. Change one rendering variable at a time and retain comparable before/after evidence.
5. Apply low-risk fixes before considering a QRhi/glyph-atlas renderer.
6. Keep QML as the application shell unless separate evidence demonstrates a shell-level bottleneck that cannot be fixed
   locally.

Raw terminal contents, credentials, profile data, and command history are excluded from performance reports. Workload
identity is represented by fixed scenario metadata rather than captured output.

## Consequences

- Release builds can diagnose renderer cost without relying on Debug-only logging.
- Runtime benchmarks require a real interactive desktop; CI unit tests validate the recorder but cannot claim GPU frame
  performance.
- Performance artifacts live under the build tree and are not source-controlled.
- Optimizations may be rejected even when they look architecturally attractive if they do not improve the agreed metrics.
- A future QRhi renderer remains possible, but it requires an explicit measured decision rather than an assumption that GPU
  code is automatically faster.

