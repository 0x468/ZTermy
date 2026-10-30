# 0152: Bounded D3D11 terminal texture updates

Status: Accepted after focused pixel, resource-lifecycle and rotated workload gates.

## Context

ADR 0151 avoids rerasterizing unchanged text, but still creates a full viewport
texture for each content frame. Sparse output in a large viewport therefore
continues to submit a large source image to the graphics backend.

## Decision

On D3D11, retain a node-owned BGRA8 QRhi texture at the same physical size and
update it through QSGTexture::commitTextureOperations. A changed size or RHI
owner creates a new texture; scene graph node destruction owns its lifetime.
Unsupported backends, formats and failed allocations retain Qt's factory path.
The CPU renderer, text shaping, overlays and terminal state remain unchanged.

Only compatible same-position row-raster keys may authorize partial uploads.
Upload full-width physical bands covering every missed row, including conservative
pixel margins. Do not trust producer damage lists, hash equality or snapshot
delivery continuity. First paint, cache invalidation and complex render states
upload the full image. More than eight bands or over half the viewport area also
falls back to full upload. Empty or pending changes must not discard an earlier
unsubmitted region: union pending bands until commit and retain the latest image.

For partial updates, the queued resource batch owns independent rectangular
image copies, not the complete CPU canvas. Retaining the latter may force a
whole-image detach at the next paint. Full uploads may retain a shared full
image. No second persistent full-screen CPU cache is introduced. Existing row
cache limits and hidden-viewport scheduling remain in effect.

Keep internal process overrides `ZTERMY_TEXTURE_UPLOAD_MODE=factory|full|bands`
for same-build comparisons; default to bands on the supported backend. There is
no persisted user setting or schema change. `ZTERMY_TEXTURE_METRICS=1` enables
upload accounting and supported whole-frame GPU timestamps before scene graph
initialization. Submitted source bytes, estimated factory bytes and CPU enqueue
time are separate metrics; none is a measured bus-transfer time. Whole-frame
GPU timing is not an isolated upload timer or end-to-end input latency.

## Compatibility cost

This introduces Qt::GuiPrivate because QRhi has limited source/binary
compatibility. Both static and dynamic Release builds must use the same pinned
Qt 6.8.3 headers and libraries. Recheck this boundary on Qt upgrades; do not
silently extend it to other backends or replace fallback behavior to make an
upgrade compile.

References:

- [QSGTexture upload hook](https://doc.qt.io/qt-6.8/qsgtexture.html#commitTextureOperations)
- [QRhi compatibility](https://doc.qt.io/qt-6.8/qrhi.html)
- [Texture upload source ownership and rectangles](https://doc.qt.io/qt-6.8/qrhitexturesubresourceuploaddescription.html)

## Acceptance

Compare identical executables and synthetic workloads with rotating factory,
full-reuse and partial-update order. Retain raw samples, failures and outliers;
do not build or run unrelated tests during timed sampling. Record CPU/process
memory as well as GPU metrics, not just reduced upload bytes.

Compare final pixels to Qt's original texture factory, not merely two custom
upload modes, at DPR 1, 1.25 and 2. Cover selection, scrolling, fonts, spacing,
images, cursor, IME, highlights, transparency and first/last visible rows.
Verify same-size texture reuse, conservative full fallback, resize recreation,
software-backend fallback and hidden/restore behavior. Evidence remains local
under `build/texture-upload-study-20261030/`.

The second prototype's three rotated runs reduced sampled process CPU from
2687.50ms to 2468.75ms and sampled private-memory peak from 365.61MiB to
325.85MiB (medians, including startup). At 160x45, whole-frame GPU means had
medians of 1289.70us versus 320.78us. At 240x65 the corresponding 1539.42us
versus 1494.58us difference was small relative to variability; do not claim a
stable large-window GPU speedup. Source upload bytes fell from a full-image
estimate of 999,979,200 to 35,513,280 per 120 large sparse updates, not a
96-percent overall performance improvement. An earlier shared-full-image
prototype regressed CPU paint and was replaced, not silently adopted.

Twenty-seven factory-reference pixel scenarios and the resource-counter test
passed at DPR 1, 1.25 and 2. Software-backend pixel fallbacks passed separately;
the D3D11-specific counter test is intentionally skipped there.
