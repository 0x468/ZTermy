#pragma once

#include <ghostty/vt.h>
#include <cstddef>
#include <cstdint>

// Initialization only, before any terminal worker starts. Unlimited until the
// application installs its shared policy; usage counts native stored pixels.
extern "C" GhosttyResult ztermy_ghostty_configure_image_budget(std::size_t bytes);
extern "C" std::size_t ztermy_ghostty_image_budget_usage();
struct ZtermyGhosttyImageReclaimer final
{
    bool (*callback)(void *, std::size_t, std::uint32_t, bool) = nullptr;
    void *userdata = nullptr;
};
extern "C" ZtermyGhosttyImageReclaimer ztermy_ghostty_exchange_image_reclaimer(ZtermyGhosttyImageReclaimer value);

struct ZtermyGhosttyImageRecord final
{
    std::uint32_t imageId;
    std::uint32_t visible;
    std::uint64_t generation;
    std::uint64_t bytes;
    std::uint64_t screenGeneration;
};
static_assert(sizeof(ZtermyGhosttyImageRecord) == 32);

// Only call while holding the terminal's engine gate. Inventory reports the required
// count on insufficient capacity; eviction revalidates both identity and visibility.
extern "C" GhosttyResult ztermy_ghostty_image_inventory(GhosttyTerminal terminal, bool alternate,
                                                        ZtermyGhosttyImageRecord *records, std::size_t capacity,
                                                        std::size_t *count);
extern "C" GhosttyResult ztermy_ghostty_evict_image(GhosttyTerminal terminal, bool alternate, std::uint32_t id,
                                                    std::uint64_t screenGeneration, std::uint64_t generation);

struct ZtermyGhosttyUnicodePlacement final
{
    std::uint32_t imageId;
    std::uint32_t placementId;
    std::int32_t column;
    std::int32_t row;
    double offsetX;
    double offsetY;
    double width;
    double height;
    double sourceX;
    double sourceY;
    double sourceWidth;
    double sourceHeight;
};
static_assert(sizeof(ZtermyGhosttyUnicodePlacement) == 80);

extern "C" GhosttyResult ztermy_ghostty_unicode_placements(GhosttyTerminal terminal,
                                                           ZtermyGhosttyUnicodePlacement *placements,
                                                           std::size_t capacity, std::size_t *count);

// Local pinned-dependency extension. Copies borrowed RGBA pixels into Ghostty's
// allocator/storage without touching its VT parser, upload state or text cursor.
extern "C" GhosttyResult ztermy_ghostty_insert_image(GhosttyTerminal terminal, const std::uint8_t *pixels,
                                                     std::size_t length, std::uint32_t width, std::uint32_t height,
                                                     std::uint32_t numerator, std::uint32_t denominator, bool absolute);
