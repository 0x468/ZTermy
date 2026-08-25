#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ztermy::terminal
{

enum class ShellDialect : std::uint8_t
{
    PowerShell,
    Posix,
    Cmd,
};

[[nodiscard]] std::string quoteShellPath(std::string_view path, ShellDialect dialect);
[[nodiscard]] std::string quoteShellPaths(std::span<const std::string> paths, ShellDialect dialect);

} // namespace ztermy::terminal
