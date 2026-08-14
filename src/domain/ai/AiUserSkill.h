#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

enum class AiUserSkillWarning : std::uint8_t
{
    symbolicLink,
    missingSkillFile,
    unreadableSkillFile,
    skillFileTooLarge,
    invalidUtf8,
    missingFrontmatter,
    missingName,
    invalidName,
    nameDirectoryMismatch,
    missingDescription,
    descriptionTooLong,
    compatibilityTooLong,
    emptyInstructions,
    disallowedControl,
    catalogueLimit,
};

struct AiUserSkill final
{
    std::string id;
    std::string name;
    std::string description;
    std::string instructions;
    std::vector<AiUserSkillWarning> warnings;
    bool ready = false;

    [[nodiscard]] friend bool operator==(const AiUserSkill &, const AiUserSkill &) = default;
};

struct AiUserSkillLimits final
{
    std::size_t maximumSkills = 200;
    std::size_t maximumSkillBytes = std::size_t{128} * 1024;
    std::size_t maximumNameCharacters = 64;
    std::size_t maximumDescriptionCharacters = 1024;
    std::size_t maximumCompatibilityCharacters = 500;
};

[[nodiscard]] bool validAiUserSkillId(std::string_view id) noexcept;
[[nodiscard]] bool containsDisallowedAiUserSkillControl(std::string_view text) noexcept;

} // namespace ztermy::ai
