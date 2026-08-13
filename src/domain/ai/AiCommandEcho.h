#pragma once

#include <string>
#include <string_view>

namespace ztermy::ai
{

// Synthetic echo for agent commands: a single-line shell comment that makes
// the agent's action visible in the terminal without executing anything and
// without entering the shell history. Modeled on the Netcatty synthetic-echo
// design (research section 8.1): the user watches the command appear and run
// instead of wondering what the agent is doing.
class AiCommandEcho final
{
public:
    // shellKind tokens: "powershell", "bash", "zsh", "fish", "cmd", or empty
    // for an unknown POSIX-compatible shell.
    [[nodiscard]] static std::string markerLine(std::string_view command, std::string_view shellKind);
};

} // namespace ztermy::ai
