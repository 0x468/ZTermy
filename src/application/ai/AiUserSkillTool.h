#pragma once

#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/AiUserSkill.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

class AiUserSkillTool final
{
public:
    [[nodiscard]] static std::vector<AiToolDefinition> definitions(std::span<const AiUserSkill> skills);
    [[nodiscard]] static std::string execute(std::string_view toolName, std::string_view argumentsJson,
                                             std::span<const AiUserSkill> skills);
    [[nodiscard]] static std::string selectedInstructions(std::span<const AiUserSkill> skills,
                                                          std::span<const std::string> selectedIds);
};

} // namespace ztermy::ai
