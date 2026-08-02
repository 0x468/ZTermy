#include "domain/workbench/QuickCommand.h"

#include <algorithm>
#include <string_view>

namespace
{

constexpr std::size_t maximumIdBytes = 64;
constexpr std::size_t maximumNameBytes = 128;
constexpr std::size_t maximumDescriptionBytes = 1024;
constexpr std::size_t maximumCommandBytes = std::size_t{64} * 1024;

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

namespace ztermy::workbench
{

bool validQuickCommand(const QuickCommand &quickCommand) noexcept
{
    return !quickCommand.id.empty() && quickCommand.id.size() <= maximumIdBytes
           && !containsDisallowedControl(quickCommand.id, false) && !quickCommand.name.empty()
           && quickCommand.name.size() <= maximumNameBytes && !containsDisallowedControl(quickCommand.name, false)
           && !quickCommand.command.empty() && quickCommand.command.size() <= maximumCommandBytes
           && !containsDisallowedControl(quickCommand.command, true)
           && quickCommand.description.size() <= maximumDescriptionBytes
           && !containsDisallowedControl(quickCommand.description, false) && quickCommand.createdUtcMs >= 0
           && quickCommand.modifiedUtcMs >= quickCommand.createdUtcMs;
}

} // namespace ztermy::workbench
