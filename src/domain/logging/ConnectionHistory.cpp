#include "domain/logging/ConnectionHistory.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>

namespace
{

[[nodiscard]] bool bounded(const std::string &value, const std::size_t maximum, const bool required = false) noexcept
{
    return (!required || !value.empty()) && value.size() <= maximum && value.find('\0') == std::string::npos;
}

[[nodiscard]] bool oneOf(const std::string_view value, const std::initializer_list<std::string_view> options) noexcept
{
    return std::ranges::find(options, value) != options.end();
}

} // namespace

namespace ztermy::logging
{

bool validConnectionHistoryEntry(const ConnectionHistoryEntry &entry) noexcept
{
    return bounded(entry.id, 128, true) && bounded(entry.sessionId, 128, true) && bounded(entry.profileId, 128)
           && bounded(entry.hostLabel, 256, true) && bounded(entry.hostname, 1024, true) && bounded(entry.username, 256)
           && bounded(entry.protocol, 32, true) && bounded(entry.localUsername, 256, true)
           && bounded(entry.localHostname, 256, true)
           && oneOf(entry.status, {"connecting", "connected", "failed", "disconnected", "interrupted"})
           && bounded(entry.phase, 64) && bounded(entry.failure, 512) && bounded(entry.rawLogPath, 4096)
           && entry.startedUtcMs > 0 && entry.endedUtcMs >= 0
           && (entry.endedUtcMs == 0 || entry.endedUtcMs >= entry.startedUtcMs);
}

void pruneConnectionHistory(ConnectionHistory &history)
{
    constexpr std::size_t maximumEntries = 2'000;
    constexpr std::size_t maximumUnsavedEntries = 1'000;
    std::ranges::sort(history.entries, std::greater{}, &ConnectionHistoryEntry::startedUtcMs);
    std::size_t unsaved = 0;
    std::erase_if(history.entries, [&unsaved](const ConnectionHistoryEntry &entry) {
        if (entry.saved)
        {
            return false;
        }
        ++unsaved;
        return unsaved > maximumUnsavedEntries;
    });
    if (history.entries.size() > maximumEntries)
    {
        history.entries.resize(maximumEntries);
    }
}

} // namespace ztermy::logging
