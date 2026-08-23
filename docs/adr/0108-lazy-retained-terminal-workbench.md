# ADR 0108: Lazy-retained terminal workbench

- Status: Accepted
- Date: 2026-08-23

## Context

The terminal workbench contains history, scripts, SFTP, composition, settings, and the built-in AI assistant. It was
instantiated for every terminal page even when closed. A reproducible Release UI benchmark measured 10,166 `QObject`
instances and 4,432 `QQuickItem` instances after opening a local terminal but before opening the workbench.

Qt documents `Loader` as the standard way to delay creation of components that are not yet required. Qt also recommends
lazy-loading non-critical QML modules and services when optimizing startup. The decision still needs product evidence:
destroying the workbench on every close saves memory but adds repeated creation latency and can discard transient UI state.

## Decision

Instantiate `TerminalWorkbench` through a synchronous `Loader` on first request. Retain the created subtree after it is
closed and hide it with `visible: false`; do not destroy and recreate it for normal page switching.

Keep the first load synchronous because the workbench is explicitly requested and must be ready before interaction. Do
not enable asynchronous incubation until measurement demonstrates that its progressive creation improves responsiveness
without exposing partially loaded controls.

The UI benchmark records closed/open object counts, first-open latency, close/reopen latency, streaming heartbeat, and
frame swaps so this policy remains testable.

## Evidence

Five eager warm baselines and repeated lazy-retained candidates on the same static Release D3D11 configuration measured:

- QML load median: 469 ms to 418 ms (-10.9%).
- closed-state object count: 10,166 to 7,139 (-29.8%).
- closed-state QQuickItem count: 4,432 to 3,110 (-29.8%).
- first-open median: 48 ms to 93 ms (+45 ms).
- retained reopen median: 20 ms.
- 240-chunk stream median: 2,588 ms to 2,601 ms (+0.5%).
- maximum GUI heartbeat gap: 146 ms to 136 ms (-6.8%).

## Consequences

- Users who never open the workbench avoid constructing roughly three thousand QML objects.
- First use has a measured sub-100 ms creation cost; later use is fast and preserves transient workbench state.
- Once opened, the workbench retains its memory for the process lifetime. Memory-pressure-driven unloading is not added
  without measured need.
- The QML shell remains appropriate. This result does not justify a QWidget rewrite; the measured hotspot was eager object
  creation and can be addressed locally.
- Workbench pages may later be split into their own lazy subtrees, but only if profiling identifies a specific page as the
  dominant retained cost.

## References

- [Qt 6.8 Loader](https://doc.qt.io/qt-6.8/qml-qtquick-loader.html)
- [Qt Quick performance considerations](https://doc.qt.io/qt-6/qtquick-performance.html)
