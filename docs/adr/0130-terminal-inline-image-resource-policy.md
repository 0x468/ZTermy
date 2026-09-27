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
- Calculate same-ID replacement pressure from the net pixel-size increase,
  excluding its old raster from prospective storage usage. Do not evict that
  replacement target while freeing capacity for its successor. Exact reclaimed
  capacity satisfies a request; do not evict another image merely because the
  reclaimed byte count equals the requested count. Cross-session visibility-aware
  eviction is implemented separately by the shared policy below.
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
- Supply a process-lifetime host allocator to the native terminal state. The
  Windows full-app image replacement workload grew outside NT heaps with the
  default allocator; a terminal-only host-allocator A/B removed that slope.
  Use paired C++ aligned nothrow allocation/deallocation, interpret alignment
  according to the pinned Zig bridge's log2 representation, and decline optional
  resize/remap without touching the original allocation. This leaves copying
  and failure recovery to the engine. Other native handles keep their current
  allocator; do not free their buffers through the host adapter. This is not a
  total-memory cap, nor does it replace the per-screen image budgets.
- Reuse the last immutable raster's Qt pixel view within each image paint call.
  Consecutive placements of a gray-alpha image must not repeat full RGBA
  conversion. Retain at most one converted raster and release it before changing
  source; this is not a persistent or cross-frame image cache.
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
- Kitty animation frame transmission, playback control and frame composition
  remain unsupported. Reject these commands with an identified `ENOTSUP`
  response rather than discarding the image ID and silently losing the error.
  Preserve the caller's `i` or `I` and the protocol's quiet-response semantics;
  specifying both produces `EINVAL`. Existing static image data is unchanged.
  No animation frames are retained or timers started by these commands. This
  is an explicit compatibility boundary, not full Kitty animation support.
  Reference: [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/#animation).
- Static Kitty/Sixel images remain terminal content at every effects tier and
  in performance mode; never silently hide them as decorative motion. SGR text
  blinking follows the application's motion policy: reduced/off effects and
  performance mode keep the text visible without blinking. Performance mode
  overrides the effective tier without erasing the saved preference. Repeated
  image replacements remain ordinary output subject to snapshot coalescing,
  not an application animation timer.

## Consequences and unfinished validation

### Implemented cross-session policy (2026-09-27; bounded validation)

The owner approved a shared decoded-image storage budget: reclaim older images
that are no longer visible first, protect currently visible placements across
all windows and panes, and reject new admission when no eligible victim exists.
An inactive pane is not necessarily invisible. Removing a history image may be
irreversible unless the client retransmits it; do not imply automatic recovery.
The application initializes a 128-MiB process-wide stored-raster limit at startup,
before constructing sessions. This is a bounded default, not a per-pane quota or
an assurance that every concurrent upload will fit. Eight-engine synthetic
sampling and its tradeoffs are recorded in the terminal research document.
This budget does not cap loader scratch space, in-flight snapshots or GPU memory.

Native storage now maintains a process-wide atomic charge. Admission reserves
only a replacement's positive delta before commit; failures return reservations,
shrinking returns the difference, and the stored raster owns its remaining charge
until deletion or terminal destruction. Kitty and the Sixel insertion extension
share this path. A startup-only configuration API can impose a total ceiling;
the application installs the default through its engine initialization interface.
Existing per-screen limits still apply, with admission
using the same visibility protection rather than unconditional oldest-image eviction.
Standalone library consumers remain unlimited until they explicitly configure it.

Integration must cover both Kitty admission and decoded Sixel insertion before
storage commits; dropping a renderer snapshot after successful admission is not
equivalent. Account replacements by their net storage change, preserve the old
image if admission fails, and release reservations on errors and session teardown.
Coordinate with the pinned engine's existing per-screen eviction rather than
allowing that path to independently discard protected images. Eviction must use
the victim engine's serialization gate, revalidate visibility/generation, release
tracked placement pins and dirty its image storage. Never access a foreign raw
terminal handle without its gate or block the GUI awaiting another worker.

For local sessions, "owning worker" means the session's serialized background
execution domain, not one fixed thread ID: output feeding runs on the read
worker, commands on the write worker, and snapshot production shares
`m_engineMutex`. SSH instead services its queue in one I/O loop with an existing
command wake event. The initial queue-and-wait proposal is superseded by a
non-blocking engine gate: all 23 public state operations serialize on a recursive
mutex (some public operations call others). Admission may only try-lock foreign
gates, never wait for one. Busy engines are temporarily ineligible; pressure can
still reject an upload rather than stall terminal interaction. The registry lock
is released before entering any engine. A shared lifetime token is invalidated
under the gate before native teardown, so copied candidate lists do not retain
or dereference freed terminals. This changes serialization, not session ownership.

On global admission failure, a thread-local scoped callback inventories idle
engines, orders non-visible candidates by the native process-global generation,
and revalidates each before deletion. The requesting image ID is excluded from
its own engine's candidates. Only invisible content is removed; current viewports
do not require forced repaint, and storage generation changes reach subsequent
normal snapshots/scrolls. The callback returns before retrying atomic admission;
concurrent contenders may still consume the recovered space first. No waiting,
VT replay, GUI callbacks or cross-session command injection is involved.

Single-screen capacity pressure invokes the same callback with a screen-only
scope: only the requesting terminal's active screen can supply candidates.
Freeing another screen cannot satisfy this local limit. With all stored images
visible, a new raster is rejected; after an image scrolls into history, it can be
reclaimed. A replacement's existing ID remains protected throughout admission.

Targeted failure cases are concurrent admission at the shared limit, all-visible
content rejecting a new image without damage, history-only eviction, a single
image with both visible and offscreen placements, stale eviction after viewport
scrolling, failed replacement, and budget recovery after deletion/session close.
Tests must exercise the engine admission paths, not only an isolated counter.

Compressed input, decoded output, allocator capacity, immutable in-flight
snapshots and GPU textures can coexist. These per-buffer/per-screen limits are
not a hard bound on total process memory. Shared-budget admission and invisible
image eviction have focused coverage; broader renderer/GPU pressure and long-run
workloads remain validation boundaries. A loopback SSH check
now covers a stalled GUI during 80 replacements of a 128-by-128 RGB image:
the worker answers a trailing CPR request before the UI resumes, recovery delivers
at most two snapshots, and the original image's weak reference expires. This is
bounded worker/UI handoff evidence, not a whole-process memory ceiling. Software-backend
full-app checks verify static image visibility across effects tiers and performance
mode; they do not establish native GPU memory usage or complete blink timing.

The subsequent four-round full-app replacement workload used confirmed D3D11
and a software-backend control. Dedicated GPU memory returned to its sampled
baseline after deletions, but private process memory kept rising on both
backends. Single-thread RGB and mid-upload-snapshot probes plateaued instead.
Heap/address-space comparison and a terminal-only host-allocator A/B isolated a
mitigation. The production adapter removes the measured D3D11 four-round slope;
see before/after evidence and remaining stress boundaries in
`docs/research/TERMINAL_ENGINE.md`. Do not equate these bounded workloads or the
existing lifetime unit tests with proof that every application path is leak-free.

Normal output must recover after rejected images. Tests must use valid compressed
over-limit rasters (not merely malformed input that every decoder would reject),
and verify subsequent text and images still work. Full-app acceptance and the
remaining protocol/resource-policy checks are still required; do not advertise
full protocol support based on these resource limits alone.
