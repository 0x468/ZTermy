#include "domain/workbench/CommandHistoryIndex.h"

#include <algorithm>
#include <limits>
#include <ranges>

namespace
{

[[nodiscard]] bool validText(const std::string &value, const std::size_t maximumBytes, const bool required) noexcept
{
    return (!required || !value.empty()) && value.size() <= maximumBytes
           && std::ranges::none_of(value, [](const unsigned char character) {
                  return character == 0 || character == 0x7F || (character < 0x20 && character != '\t');
              });
}

} // namespace

namespace ztermy::workbench
{

bool validIndexedCommand(const IndexedCommand &entry) noexcept
{
    return validText(entry.command, std::size_t{64} * 1024, true) && validText(entry.sourceId, 256, false)
           && validText(entry.sourceLabel, 256, false) && entry.firstUsedUtcSeconds >= 0
           && entry.lastUsedUtcSeconds >= entry.firstUsedUtcSeconds && entry.useCount > 0;
}

void recordIndexedCommand(CommandHistoryIndex &index, IndexedCommand entry, const std::size_t maximumEntries)
{
    if (!validIndexedCommand(entry) || maximumEntries == 0)
    {
        return;
    }
    const auto existing = std::ranges::find_if(index.entries, [&entry](const IndexedCommand &candidate) {
        return candidate.command == entry.command && candidate.sourceId == entry.sourceId;
    });
    if (existing != index.entries.end())
    {
        entry.firstUsedUtcSeconds = existing->firstUsedUtcSeconds;
        entry.lastUsedUtcSeconds = (std::max)(existing->lastUsedUtcSeconds, entry.lastUsedUtcSeconds);
        entry.useCount = existing->useCount == (std::numeric_limits<std::uint32_t>::max)() ? existing->useCount
                                                                                           : existing->useCount + 1;
        index.entries.erase(existing);
    }
    index.entries.insert(index.entries.begin(), std::move(entry));
    if (index.entries.size() > maximumEntries)
    {
        index.entries.resize(maximumEntries);
    }
}

} // namespace ztermy::workbench
