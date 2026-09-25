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
- Limit APC buffers to 16 KiB. Kitty tools must use chunked direct transmission
  (the protocol's 4096-byte base64 chunks fit). Disable filesystem, temporary
  filesystem and shared-memory media explicitly.
- PNG has an additional 8-MiB encoded-input limit and metadata is not inflated.
  Sixel also has explicit decoder input/work budgets. Snapshot ownership remains
  immutable across worker/render threads and cache references are weak.

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
