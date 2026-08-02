#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::workbench
{

enum class ShellKind : std::uint8_t
{
    unknown,
    bash,
    zsh,
    fish,
    powershell,
};

struct ShellHistoryEntry final
{
    std::string command;
    ShellKind shell = ShellKind::unknown;
    std::optional<std::int64_t> timestampUtcSeconds;

    bool operator==(const ShellHistoryEntry &) const = default;
};

[[nodiscard]] std::vector<ShellHistoryEntry> parseBashHistory(std::string_view contents,
                                                              std::size_t maximumEntries = 1000);
[[nodiscard]] std::vector<ShellHistoryEntry> parseZshHistory(std::string_view contents,
                                                             std::size_t maximumEntries = 1000);
[[nodiscard]] std::vector<ShellHistoryEntry> parseFishHistory(std::string_view contents,
                                                              std::size_t maximumEntries = 1000);
[[nodiscard]] std::vector<ShellHistoryEntry> parsePowerShellHistory(std::string_view contents,
                                                                    std::size_t maximumEntries = 1000);

} // namespace ztermy::workbench
