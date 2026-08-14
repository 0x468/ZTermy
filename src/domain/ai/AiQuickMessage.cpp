#include "domain/ai/AiQuickMessage.h"

#include <algorithm>

namespace
{

constexpr std::size_t maximumIdBytes = 64;
constexpr std::size_t maximumNameBytes = 480;
constexpr std::size_t maximumSlugBytes = 48;
constexpr std::size_t maximumDescriptionBytes = 960;
constexpr std::size_t maximumContentBytes = std::size_t{10} * 1024;

[[nodiscard]] bool containsDisallowedControl(const std::string_view value, const bool multiline) noexcept
{
    return std::ranges::any_of(value, [multiline](const unsigned char character) {
        if (character == 0x7F || character == 0x1B)
        {
            return true;
        }
        if (character >= 0x20)
        {
            return false;
        }
        return !multiline || (character != '\t' && character != '\n' && character != '\r');
    });
}

} // namespace

namespace ztermy::ai
{

bool validAiQuickMessageSlug(const std::string_view slug) noexcept
{
    if (slug.empty() || slug.size() > maximumSlugBytes || slug.front() == '-' || slug.back() == '-')
    {
        return false;
    }
    bool previousHyphen = false;
    for (const unsigned char character : slug)
    {
        const bool hyphen = character == '-';
        if ((!hyphen && !std::isdigit(character) && !(character >= 'a' && character <= 'z'))
            || (hyphen && previousHyphen))
        {
            return false;
        }
        previousHyphen = hyphen;
    }
    return true;
}

std::string normalizeAiQuickMessageSlug(const std::string_view value)
{
    std::string normalized;
    normalized.reserve(std::min(value.size(), maximumSlugBytes));
    bool pendingHyphen = false;
    for (const unsigned char character : value)
    {
        const bool asciiDigit = character >= '0' && character <= '9';
        const bool asciiLower = character >= 'a' && character <= 'z';
        const bool asciiUpper = character >= 'A' && character <= 'Z';
        if (asciiDigit || asciiLower || asciiUpper)
        {
            if (pendingHyphen && !normalized.empty() && normalized.size() < maximumSlugBytes)
            {
                normalized.push_back('-');
            }
            pendingHyphen = false;
            if (normalized.size() >= maximumSlugBytes)
            {
                break;
            }
            normalized.push_back(static_cast<char>(asciiUpper ? character - 'A' + 'a' : character));
            continue;
        }
        pendingHyphen = !normalized.empty();
    }
    while (!normalized.empty() && normalized.back() == '-')
    {
        normalized.pop_back();
    }
    return normalized;
}

bool validAiQuickMessage(const AiQuickMessage &message) noexcept
{
    return !message.id.empty() && message.id.size() <= maximumIdBytes && !containsDisallowedControl(message.id, false)
           && !message.name.empty() && message.name.size() <= maximumNameBytes
           && !containsDisallowedControl(message.name, false) && validAiQuickMessageSlug(message.slug)
           && !message.content.empty() && message.content.size() <= maximumContentBytes
           && !containsDisallowedControl(message.content, true) && message.description.size() <= maximumDescriptionBytes
           && !containsDisallowedControl(message.description, false) && message.createdUtcMs >= 0
           && message.modifiedUtcMs >= message.createdUtcMs;
}

} // namespace ztermy::ai
