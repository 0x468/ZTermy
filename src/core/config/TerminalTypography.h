#pragma once

#include <cmath>

namespace ztermy::config
{
// Font points map to Windows/Qt logical pixels, never physical display pixels.
inline constexpr double terminalPixelsPerPoint = 96.0 / 72.0;

// Grid scales multiply the font's natural metrics, not its em size: 1 keeps
// native spacing. In particular, cellWidth=0.6 compresses native advance to 60%.

[[nodiscard]] inline bool validTerminalTypography(const double points, const int weight, const double lineHeight,
                                                  const double cellWidth) noexcept
{
    return std::isfinite(points) && points >= 6.0 && points <= 48.0 && weight >= 100 && weight <= 900
           && weight % 100 == 0 && std::isfinite(lineHeight) && lineHeight >= 1.0 && lineHeight <= 3.0
           && std::isfinite(cellWidth) && cellWidth >= 0.5 && cellWidth <= 3.0;
}
} // namespace ztermy::config
