#include "domain/ai/AiUserSkill.h"

namespace ztermy::ai
{

bool validAiUserSkillId(const std::string_view id) noexcept
{
    if (id.empty() || id.size() > 64 || id.front() == '-' || id.back() == '-')
    {
        return false;
    }

    bool previousWasHyphen = false;
    for (const unsigned char character : id)
    {
        const bool hyphen = character == '-';
        const bool lowercaseAscii = character >= 'a' && character <= 'z';
        const bool digitAscii = character >= '0' && character <= '9';
        if ((!lowercaseAscii && !digitAscii && !hyphen) || (hyphen && previousWasHyphen))
        {
            return false;
        }
        previousWasHyphen = hyphen;
    }
    return true;
}

bool containsDisallowedAiUserSkillControl(const std::string_view text) noexcept
{
    for (const unsigned char character : text)
    {
        if ((character < 0x20U && character != '\n' && character != '\r' && character != '\t')
            || character == 0x7FU)
        {
            return true;
        }
    }
    return false;
}

} // namespace ztermy::ai
