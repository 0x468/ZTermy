#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ztermy::logging
{

struct ConnectionHistoryEntry final
{
    std::string id;
    std::string sessionId;
    std::string profileId;
    std::string hostLabel;
    std::string hostname;
    std::string username;
    std::string protocol;
    std::string localUsername;
    std::string localHostname;
    std::string status;
    std::string phase;
    std::string failure;
    std::string rawLogPath;
    std::int64_t startedUtcMs = 0;
    std::int64_t endedUtcMs = 0;
    bool saved = false;

    friend bool operator==(const ConnectionHistoryEntry &, const ConnectionHistoryEntry &) = default;
};

struct ConnectionHistory final
{
    std::vector<ConnectionHistoryEntry> entries;

    friend bool operator==(const ConnectionHistory &, const ConnectionHistory &) = default;
};

[[nodiscard]] bool validConnectionHistoryEntry(const ConnectionHistoryEntry &entry) noexcept;
void pruneConnectionHistory(ConnectionHistory &history);

} // namespace ztermy::logging
