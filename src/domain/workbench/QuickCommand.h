#pragma once

#include <cstdint>
#include <string>

namespace ztermy::workbench
{

enum class ShellScope : std::uint8_t
{
    any,
    posix,
    powershell,
};

struct QuickCommand final
{
    std::string id;
    std::string name;
    std::string command;
    std::string description;
    ShellScope shellScope = ShellScope::any;
    std::int64_t createdUtcMs = 0;
    std::int64_t modifiedUtcMs = 0;

    bool operator==(const QuickCommand &) const = default;
};

[[nodiscard]] bool validQuickCommand(const QuickCommand &quickCommand) noexcept;

} // namespace ztermy::workbench
