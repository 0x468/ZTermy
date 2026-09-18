#pragma once

#include <array>
#include <cstdint>

namespace ztermy::terminal
{

struct TerminalColor
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    friend bool operator==(const TerminalColor &, const TerminalColor &) = default;
};

// Default colors and the 16 ANSI entries a terminal theme provides. The engine
// keeps the 6x6x6 cube and the gray ramp from its built-in palette.
struct TerminalColorScheme
{
    TerminalColor foreground{.red = 248, .green = 250, .blue = 252};
    TerminalColor background{.red = 11, .green = 16, .blue = 23};
    TerminalColor cursor{.red = 248, .green = 250, .blue = 252};
    std::array<TerminalColor, 16> ansi{};

    friend bool operator==(const TerminalColorScheme &, const TerminalColorScheme &) = default;
};

} // namespace ztermy::terminal
