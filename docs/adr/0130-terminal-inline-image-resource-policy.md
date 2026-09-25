# 0130: Bound inline images before rendering

Status: Accepted (implementation in progress)

## Context

Kitty and Sixel are terminal output, including output from untrusted remote
programs. A viewport snapshot limit alone cannot bound the earlier transmission,
inflation and decode allocations. The pinned Ghostty revision exposes a storage
limit but uses a separate 400-MiB loader/inflater limit.

## Decision

- Reuse Ghostty image storage and immutable domain snapshots for inline images.
- Insert decoded Sixel pixels through a small ztermy-owned Zig/C extension to
  the pinned dependency. It copies pixels using the terminal's allocator and
  creates a tracked screen placement; it never feeds a synthetic Kitty upload
  into the active VT parser. This keeps incomplete Kitty uploads independent.
  Absolute-screen placement starts at the active screen origin and clips there,
  without disturbing cursor position, saved cursor or pending soft wrap.
- Limit one image's loader/inflater payload to 32 MiB and dimensions to 8192.
  Apply a mechanical, fail-closed patch to the pinned dependency during fetching;
  do not maintain a copied third-party implementation. Dependency updates must
  review and eventually replace this patch with an upstream configuration API.
- Preserve the initial multipart image ID in error replies, and discard the
  loading buffer when appending a chunk exceeds its budget. Otherwise rejection
  can be silent or leave later images stuck behind the failed transmission.
- Limit image storage to 32 MiB per terminal screen. Primary and alternate
  screens are separate; this is not a process-wide 32-MiB budget.
- Independently cap stored image records and placement records at 4096 each
  per screen. Tiny images otherwise bypass the pixel-byte budget. Reject new
  records at capacity with the existing ENOMEM reply; permit replacement of
  existing IDs and resume admission after deletion. A successful explicit
  placement replacement also releases its previous screen-owned tracked pin.
- Limit APC buffers to 16 KiB. Kitty tools must use chunked direct transmission
  (the protocol's 4096-byte base64 chunks fit). Disable filesystem, temporary
  filesystem and shared-memory media explicitly.
- PNG has an additional 8-MiB encoded-input limit and metadata is not inflated.
  Sixel also has explicit decoder input/work budgets. Snapshot ownership remains
  immutable across worker/render threads and cache references are weak.
- Reuse one native placement iterator per snapshot adapter, rebinding it before
  each capture and freeing it with the adapter. Per-frame creation/free mixed
  with image upload caused native allocator footprint growth in the Windows
  probe, despite released pixel references and stable Windows heap busy counts.
- Unicode placeholder IDs, diacritics and inheritance use the pinned engine's
  iterator. ztermy computes aspect-fit fragment rectangles in floating point
  and preserves fractional source texels through the domain snapshot to Qt.
  The dependency's integer render rectangles round a quarter-pixel slice to
  zero when a one-pixel image is enlarged over four rows. Raster pixels remain
  shared; enlarging a placement must not allocate an enlarged source image.
- Local selection and IME preedit take precedence over image z-order. Restore
  selection backgrounds after below-text images, and clear only the protected
  selection/preedit rectangles from the above-text image overlay. The image
  remains visible outside those rectangles and returns after interaction ends.

## Consequences and unfinished validation

Compressed input, decoded output, allocator capacity, immutable in-flight
snapshots and GPU textures can coexist. These per-buffer/per-screen limits are
not a hard bound on total process memory. Cross-session budgeting, queue pressure,
cache eviction evidence and performance-mode behavior remain required work.

Normal output must recover after rejected images. Tests must use valid compressed
over-limit rasters (not merely malformed input that every decoder would reject),
and verify subsequent text and images still work. Full-app acceptance and the
remaining protocol/resource-policy checks are still required; do not advertise
full protocol support based on these resource limits alone.
