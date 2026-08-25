#include "domain/terminal/ShellPathQuoter.h"

#include <algorithm>

namespace ztermy::terminal
{

std::string quoteShellPath(const std::string_view path, const ShellDialect dialect)
{
    std::string result;
    result.reserve(path.size() + 8U);
    if (dialect == ShellDialect::Cmd)
    {
        result.push_back('"');
        for (const char value : path)
        {
            if (value == '"')
            {
                result.push_back('"');
            }
            result.push_back(value);
        }
        result.push_back('"');
        return result;
    }

    result.push_back('\'');
    for (const char value : path)
    {
        if (value != '\'')
        {
            result.push_back(value);
            continue;
        }
        if (dialect == ShellDialect::PowerShell)
        {
            result.append("''");
        }
        else
        {
            result.append("'\\''");
        }
    }
    result.push_back('\'');
    return result;
}

std::string quoteShellPaths(const std::span<const std::string> paths, const ShellDialect dialect)
{
    std::string result;
    for (const std::string &path : paths)
    {
        if (!result.empty())
        {
            result.push_back(' ');
        }
        result.append(quoteShellPath(path, dialect));
    }
    return result;
}

} // namespace ztermy::terminal
