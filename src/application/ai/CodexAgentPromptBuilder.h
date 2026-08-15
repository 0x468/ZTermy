#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <cstdint>
#include <expected>
#include <string>

namespace ztermy::ai
{

enum class CodexAgentPromptError : std::uint8_t
{
    empty,
    imageUnsupported,
    tooLarge,
};

class CodexAgentPromptBuilder final
{
public:
    [[nodiscard]] static std::expected<std::string, CodexAgentPromptError> build(const AiGenerationRequest &request,
                                                                                 bool continuingThread);
};

} // namespace ztermy::ai
