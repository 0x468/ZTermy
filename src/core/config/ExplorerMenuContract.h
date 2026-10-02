#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ztermy::explorer
{
// Bounded, non-secret snapshot consumed by Explorer without loading Qt or scanning shells.
inline constexpr std::array<std::string_view, 7> shellIds{
    "automatic", "powerShellCore", "windowsPowerShell", "commandPrompt", "gitBash", "nushell", "wsl"};
inline constexpr std::array<std::string_view, 7> shellNames{
    "Default shell", "PowerShell 7", "Windows PowerShell", "Command Prompt", "Git Bash", "Nushell", "WSL"};
using MenuSnapshot = std::array<std::uint8_t, 16>;
inline constexpr MenuSnapshot defaultSnapshot{'Z', 'T', 'M', 'E', 'N', 'U', '1', 0, 0, 0, 1};

[[nodiscard]] constexpr bool validSnapshot(const MenuSnapshot &value)
{
    for (std::size_t i = 0; i < 8; ++i)
        if (value[i] != defaultSnapshot[i])
            return false;
    if (value[8] > 1 || value[9] >= shellIds.size() || value[10] == 0 || value[10] > 127)
        return false;
    for (std::size_t i = 11; i < value.size(); ++i)
        if (value[i] != 0)
            return false;
    return true;
}
} // namespace ztermy::explorer
