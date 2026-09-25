#pragma once

#include <cstdint>
#include <string>

namespace ztermy::terminal
{
enum class TerminalProgressState : std::uint8_t
{
    none,
    active,
    error,
    indeterminate,
    paused,
};

struct TerminalProgress final
{
    TerminalProgressState state = TerminalProgressState::none;
    // -1 means no percentage was supplied by the program.
    int percentage = -1;
    friend bool operator==(const TerminalProgress &, const TerminalProgress &) = default;
};

struct TerminalNotification final
{
    std::uint64_t sequence = 0;
    std::string title;
    std::string body;
};
} // namespace ztermy::terminal
