#pragma once

#include <cstddef>

namespace ztermy::terminal
{
// Page-storage budget, not a process-wide RSS limit. Active screen, images,
// allocator pools and UI snapshots have independent storage lifetimes.
struct TerminalHistoryLimits final
{
    static constexpr std::size_t maximumBytes = std::size_t{64} * 1024 * 1024;
    static constexpr std::size_t maximumLines = 1000000;
    std::size_t lines = 10000;
    std::size_t bytes = maximumBytes;
};
} // namespace ztermy::terminal
