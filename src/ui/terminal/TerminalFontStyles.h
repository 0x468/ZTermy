#pragma once

#include <cstddef>

namespace ztermy::ui
{
inline constexpr std::size_t styleBold = 1U << 0U;
inline constexpr std::size_t styleItalic = 1U << 1U;
inline constexpr std::size_t styleUnderline = 1U << 2U;
inline constexpr std::size_t styleStrikeOut = 1U << 3U;
inline constexpr std::size_t styleOverline = 1U << 4U;
inline constexpr std::size_t styledFontCount = 1U << 5U;

} // namespace ztermy::ui
