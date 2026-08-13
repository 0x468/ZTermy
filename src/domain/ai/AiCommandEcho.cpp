#include "domain/ai/AiCommandEcho.h"

#include <algorithm>
#include <cctype>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] bool isPowerShell(const std::string_view shellKind)
{
    std::string lowered;
    lowered.reserve(shellKind.size());
    for (const unsigned char character : shellKind)
    {
        lowered.push_back(static_cast<char>(std::tolower(character)));
    }
    return lowered.find("pwsh") != std::string::npos || lowered.find("powershell") != std::string::npos;
}

[[nodiscard]] bool isCmd(const std::string_view shellKind)
{
    std::string lowered;
    lowered.reserve(shellKind.size());
    for (const unsigned char character : shellKind)
    {
        lowered.push_back(static_cast<char>(std::tolower(character)));
    }
    return lowered.find("cmd") != std::string::npos || lowered.find("batch") != std::string::npos
           || lowered.find("bat") != std::string::npos;
}

} // namespace

std::string AiCommandEcho::markerLine(const std::string_view command, const std::string_view shellKind)
{
    // Collapse the command to a single display line so the marker itself can
    // never break out of the comment (no newlines, no CR).
    std::string display;
    display.reserve(command.size());
    for (const char character : command)
    {
        if (character == '\r' || character == '\n' || character == '\t' || character == ' ')
        {
            // Collapse every run of whitespace (including line breaks) into a
            // single space so the marker stays one clean display line.
            if (display.empty() || display.back() != ' ')
            {
                display.push_back(' ');
            }
        }
        else
        {
            display.push_back(character);
        }
    }
    while (!display.empty() && display.back() == ' ')
    {
        display.pop_back();
    }
    if (display.empty())
    {
        display = "<empty command>";
    }
    // "rem" for cmd.exe; "#" for PowerShell and every POSIX shell. A leading
    // "#" line is not recorded in bash/zsh history and PowerShell comments
    // behave the same way.
    const std::string prefix = isCmd(shellKind) ? "rem " : "# ";
    return prefix + "[ztermy agent] $ " + display;
}

} // namespace ztermy::ai
