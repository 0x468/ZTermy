#pragma once

#include "domain/workbench/ShellHistory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ztermy::workbench
{

inline constexpr std::size_t maximumIndexedCommandCount = 5000;

struct IndexedCommand final
{
    std::string command;
    std::string sourceId;
    std::string sourceLabel;
    ShellKind shell = ShellKind::unknown;
    std::int64_t firstUsedUtcSeconds = 0;
    std::int64_t lastUsedUtcSeconds = 0;
    std::uint32_t useCount = 1;

    bool operator==(const IndexedCommand &) const = default;
};

struct CommandHistoryIndex final
{
    std::vector<IndexedCommand> entries;

    bool operator==(const CommandHistoryIndex &) const = default;
};

[[nodiscard]] bool validIndexedCommand(const IndexedCommand &entry) noexcept;
void recordIndexedCommand(CommandHistoryIndex &index, IndexedCommand entry,
                          std::size_t maximumEntries = maximumIndexedCommandCount);

} // namespace ztermy::workbench
