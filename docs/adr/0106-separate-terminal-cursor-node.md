# ADR 0106: Render the terminal cursor as a separate scene-graph node

- Status: Accepted
- Date: 2026-08-23

## Context

`TerminalItem` rasterizes the terminal surface into one `QImage` and exposes it through one scene-graph texture. The cursor
blink timer previously invalidated that entire surface every 530 ms. A Windows 11, Qt 6.8.3, Direct3D 11 Release baseline
measured four otherwise-idle cursor frames in 2.2 seconds. They repainted the full terminal image and accounted for an
estimated 13,070,592 bytes of texture upload.

Qt Quick keeps custom `QSGNode` state on the render thread, so the cursor can be represented independently without turning
terminal cells into a QML object tree.

## Decision

- Keep the terminal cells in one cached raster surface and one texture node.
- Render the terminal cursor into a small child texture node.
- Blink by changing only the child node rectangle; do not repaint or replace the terminal texture.
- Keep IME preedit rendering in the terminal surface because composition spans cells and has different cursor semantics.
- Treat font, palette, terminal snapshot, keyword, and IME changes as full invalidations. A cursor timer event is the only
  cursor-only invalidation.
- Preserve the existing full-surface fallback whenever cached state is unavailable.

## Evidence

Against the same 1120 x 800 Release baseline:

- idle paint P95: 2,000 us -> 50 us (-97.5%);
- idle paint maximum: 1,897 us -> 6 us (-99.7%);
- estimated idle terminal texture upload: 13,070,592 bytes -> 0 bytes (-100%);
- output/resize paint P95: unchanged at 4,000 us;
- output/resize maximum heartbeat gap: unchanged at 16 ms.

An earlier experiment that only marked the full texture opaque was rejected because it did not improve P95 or completion
time and increased the observed maximum heartbeat gap in that run.

## Consequences

- Idle terminal tabs consume substantially less GUI-thread painting and upload bandwidth.
- Cursor texture creation happens only when terminal content, style, or geometry changes.
- The child node lifecycle must never expose an untextured `QSGSimpleTextureNode`; a Release benchmark caught and prevented
  that failure mode during development.
- High-throughput output remains a full-surface renderer path and needs separate evidence before further optimization.
