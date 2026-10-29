# 0151: Bounded same-position terminal text raster reuse

Status: Accepted after focused pixel equivalence and paired performance gates.

## Context

Large synthetic terminal viewports spend substantially more CPU time painting
text than extracting their snapshots. Most rows remain unchanged during sparse
TUI updates. Damage lists alone are not a safe cache key: workers may coalesce
snapshots before the viewport receives them.

## Decision

Prototype reuse of the existing transparent row-ink bitmap at the same row
position only. Compare every cell against the row's independently owned content
key, including selection and style attributes. Do not hash away
correctness, assume delivery continuity, or reuse a different row after scrolling.

Non-snapshot invalidations advance a separate raster revision. Geometry, physical
size, DPR, snapshot geometry/default colors and selection mode also participate
in compatibility. Never retain a row whose cursor shaping metadata was bypassed.
The background, cursor and overlay composition paths remain unchanged.

Images, preedit, blink ink, search/highlight styles, Quick Select and Copy Mode
use the original renderer and discard the row cache. This is a deliberate first
implementation boundary, not permission to omit those features from acceptance.
Compare these fallbacks against the uncached renderer as well.

Limit retained row bitmaps and content keys to a conservatively charged 16 MiB
per viewport. Do not retain a previous snapshot. Bound cached graphemes to 16
codepoints; larger clusters take the original path without altering their text.
Oversize viewports fall back without truncating
content. This is an additional CPU raster cache, not scrollback or a GPU texture
cache. It does not avoid full texture creation/upload or stop hidden state updates.

An internal process override `ZTERMY_DISABLE_ROW_RASTER_CACHE=1` and an explicit
viewport diagnostic setter retain the original path for same-build A/B and
pixel regression. No persisted preference or schema change is introduced.

## Acceptance

Keep the prototype only after matched on/off workloads show a useful improvement
and cached output is pixel-identical to a forced uncached render. Cover selection
and deselection, scrolling with distinct row content, wide/combining characters,
font/spacing/weight/ligature changes, palette/resize, cursor, images and removal,
preedit, blink and highlight fallbacks. Repeat focused raster checks at fractional
and integer high DPI. Keep failures and do not run builds during timed sampling.

Record raw evidence locally under `build/row-raster-study-20261030/`; the local
research review records measurements and limitations, not a performance guarantee
for all hardware or network workloads.

The paired five-run synthetic 240x65 workload reduced mean CPU paint time from
11.14ms to 2.22ms (medians), with an 80.0% median paired reduction. Retained cache
data was about 9.34MiB. A noisy first pair showed almost no benefit and remains
in the statistics. This is not a GPU/upload or general throughput claim. A
20,000-line dense-output control also passed; one pair is not a throughput
guarantee. Twenty-five pixel scenarios passed at DPR 1, 1.25 and 2, including
all required conservative fallbacks.

After initial measurements, the grapheme cache boundary was narrowed from 32 to
16 codepoints to keep the conservative key-storage charge above MSVC string
growth. This does not change eligibility for the measured ASCII workloads or
ordinary wide/combining glyph fixtures; final runtime gates recheck the boundary.
