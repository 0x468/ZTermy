#include "application/ai/CodexAgentPromptBuilder.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace ztermy::ai
{
namespace
{

constexpr std::size_t maximumPromptBytes = std::size_t{900} * 1024;

[[nodiscard]] std::string_view roleName(const AiMessageRole role) noexcept
{
    switch (role)
    {
        case AiMessageRole::system:
            return "system";
        case AiMessageRole::assistant:
            return "assistant";
        case AiMessageRole::tool:
            return "tool";
        case AiMessageRole::user:
            return "user";
    }
    return "user";
}

[[nodiscard]] bool appendBounded(std::string &target, const std::string_view value)
{
    if (value.size() > maximumPromptBytes - std::min(target.size(), maximumPromptBytes))
    {
        return false;
    }
    target.append(value);
    return true;
}

} // namespace

std::expected<std::string, CodexAgentPromptError> CodexAgentPromptBuilder::build(const AiGenerationRequest &request,
                                                                                 const bool continuingThread)
{
    if (request.messages.empty())
    {
        return std::unexpected(CodexAgentPromptError::empty);
    }
    if (std::ranges::any_of(request.messages, [](const AiChatMessage &message) {
            return !message.images.empty();
        }))
    {
        return std::unexpected(CodexAgentPromptError::imageUnsupported);
    }

    std::size_t begin = 0;
    if (continuingThread)
    {
        const auto lastAssistant = std::ranges::find_last_if(request.messages, [](const AiChatMessage &message) {
            return message.role == AiMessageRole::assistant;
        });
        if (!lastAssistant.empty())
        {
            begin = static_cast<std::size_t>(std::distance(request.messages.begin(), lastAssistant.begin())) + 1;
        }
    }
    if (begin >= request.messages.size())
    {
        return std::unexpected(CodexAgentPromptError::empty);
    }

    std::string prompt;
    prompt.reserve(std::min<std::size_t>(maximumPromptBytes, 4096));
    if (!appendBounded(prompt, continuingThread ? "Current ztermy turn:\n" : "ztermy conversation and current turn:\n"))
    {
        return std::unexpected(CodexAgentPromptError::tooLarge);
    }
    for (std::size_t index = begin; index < request.messages.size(); ++index)
    {
        const AiChatMessage &message = request.messages[index];
        const std::string_view role = roleName(message.role);
        if (!appendBounded(prompt, "\n<ztermy_") || !appendBounded(prompt, role) || !appendBounded(prompt, ">\n")
            || !appendBounded(prompt, message.content) || !appendBounded(prompt, "\n</ztermy_")
            || !appendBounded(prompt, role) || !appendBounded(prompt, ">\n"))
        {
            return std::unexpected(CodexAgentPromptError::tooLarge);
        }
    }
    return prompt;
}

} // namespace ztermy::ai
