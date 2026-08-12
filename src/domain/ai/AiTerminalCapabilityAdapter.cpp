#include "domain/ai/AiTerminalCapabilityAdapter.h"

#include <algorithm>
#include <cctype>

namespace ztermy::ai
{
namespace
{
[[nodiscard]] std::string normalized(std::string_view value)
{
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] std::string shellFamily(const std::string_view shell)
{
    const std::string value = normalized(shell);
    if (value.contains("pwsh") || value.contains("powershell"))
    {
        return "powershell";
    }
    if (value.contains("bash"))
    {
        return "bash";
    }
    if (value.contains("zsh"))
    {
        return "zsh";
    }
    if (value.contains("fish"))
    {
        return "fish";
    }
    return "unknown";
}
} // namespace

AiTerminalInteractionCapability
AiTerminalCapabilityAdapter::describe(const std::string_view shell,
                                      const terminal::TerminalSemanticCapability semanticCapability,
                                      const bool alternateScreen)
{
    AiTerminalInteractionCapability result{.shellFamily = shellFamily(shell),
                                           .observationMode =
                                               alternateScreen ? "alternate_screen_frame" : "terminal_frame"};
    switch (semanticCapability)
    {
        case terminal::TerminalSemanticCapability::rich:
            result.semanticQuality = "rich_verified";
            result.exactCommandBoundaries = true;
            result.reliableExitStatus = true;
            break;
        case terminal::TerminalSemanticCapability::basic:
            result.semanticQuality = "basic_unverified";
            result.degradedReason = "shell_markers_are_not_nonce_verified";
            break;
        case terminal::TerminalSemanticCapability::none:
            result.semanticQuality = "unavailable";
            result.degradedReason =
                alternateScreen ? "command_semantics_unavailable_in_alternate_screen" : "shell_integration_unavailable";
            break;
    }
    return result;
}

} // namespace ztermy::ai
