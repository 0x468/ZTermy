#pragma once

#include <ghostty/vt.h>
#include <cstddef>
#include <cstdint>

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
