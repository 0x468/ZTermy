#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace ztermy::terminal
{
enum class TerminalImageFormat : std::uint8_t
{
    rgb,
    rgba,
    grayAlpha,
    gray,
};

// Worker-owned immutable pixels. No borrowed terminal pointers cross threads.
struct TerminalImage final
{
    std::uint64_t generation = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TerminalImageFormat format = TerminalImageFormat::rgba;
    std::vector<std::uint8_t> pixels;
};

struct TerminalImagePlacement final
{
    std::shared_ptr<const TerminalImage> image;
    std::uint32_t imageId = 0;
    std::uint32_t placementId = 0;
    std::int32_t column = 0;
    std::int32_t row = 0;
    std::int32_t z = 0;
    // Fractional texels are necessary when a small raster spans multiple rows.
    double offsetX = 0;
    double offsetY = 0;
    double width = 0;
    double height = 0;
    double sourceX = 0;
    double sourceY = 0;
    double sourceWidth = 0;
    double sourceHeight = 0;
};
} // namespace ztermy::terminal
