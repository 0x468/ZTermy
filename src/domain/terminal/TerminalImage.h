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
    std::uint32_t offsetX = 0;
    std::uint32_t offsetY = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t sourceX = 0;
    std::uint32_t sourceY = 0;
    std::uint32_t sourceWidth = 0;
    std::uint32_t sourceHeight = 0;
};
} // namespace ztermy::terminal
