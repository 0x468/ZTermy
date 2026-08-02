#include "domain/workbench/ShellHistory.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace
{

[[nodiscard]] std::vector<std::string_view> lines(const std::string_view contents)
{
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= contents.size())
    {
        const std::size_t end = contents.find('\n', start);
        std::string_view line =
            contents.substr(start, end == std::string_view::npos ? contents.size() - start : end - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        result.push_back(line);
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return result;
}

[[nodiscard]] bool hasVisibleText(const std::string_view value)
{
    return std::ranges::any_of(value, [](const unsigned char character) {
        return character != ' ' && character != '\t' && character != '\r' && character != '\n';
    });
}

void trimTrailingNewlines(std::string &value)
{
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
    {
        value.pop_back();
    }
}

[[nodiscard]] std::optional<std::int64_t> positiveInteger(const std::string_view value)
{
    if (value.empty())
    {
        return std::nullopt;
    }
    std::int64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result < 0)
    {
        return std::nullopt;
    }
    return result;
}

void appendEntry(std::vector<ztermy::workbench::ShellHistoryEntry> &entries, std::string command,
                 const ztermy::workbench::ShellKind shell, const std::optional<std::int64_t> timestamp = std::nullopt)
{
    trimTrailingNewlines(command);
    if (!hasVisibleText(command))
    {
        return;
    }
    entries.push_back({.command = std::move(command), .shell = shell, .timestampUtcSeconds = timestamp});
}

[[nodiscard]] std::vector<ztermy::workbench::ShellHistoryEntry>
newestFirst(std::vector<ztermy::workbench::ShellHistoryEntry> entries, const std::size_t maximumEntries)
{
    if (maximumEntries == 0)
    {
        return {};
    }
    if (entries.size() > maximumEntries)
    {
        entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(maximumEntries));
    }
    std::ranges::reverse(entries);
    return entries;
}

[[nodiscard]] bool bashTimestamp(const std::string_view line, std::int64_t &timestamp)
{
    if (line.size() < 2 || line.front() != '#')
    {
        return false;
    }
    const auto parsed = positiveInteger(line.substr(1));
    if (!parsed)
    {
        return false;
    }
    timestamp = *parsed;
    return true;
}

struct ZshHeader final
{
    std::int64_t timestamp = 0;
    std::string_view command;
};

[[nodiscard]] std::optional<ZshHeader> zshHeader(const std::string_view line)
{
    if (!line.starts_with(": "))
    {
        return std::nullopt;
    }
    const std::size_t timestampEnd = line.find(':', 2);
    const std::size_t commandStart =
        timestampEnd == std::string_view::npos ? std::string_view::npos : line.find(';', timestampEnd + 1);
    if (timestampEnd == std::string_view::npos || commandStart == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto timestamp = positiveInteger(line.substr(2, timestampEnd - 2));
    const auto duration = positiveInteger(line.substr(timestampEnd + 1, commandStart - timestampEnd - 1));
    if (!timestamp || !duration)
    {
        return std::nullopt;
    }
    return ZshHeader{.timestamp = *timestamp, .command = line.substr(commandStart + 1)};
}

[[nodiscard]] std::string decodeFishCommand(const std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (value[index] != '\\' || index + 1 >= value.size())
        {
            result.push_back(value[index]);
            continue;
        }
        const char escaped = value[++index];
        if (escaped == 'n')
        {
            result.push_back('\n');
        }
        else if (escaped == '\\')
        {
            result.push_back('\\');
        }
        else
        {
            result.push_back('\\');
            result.push_back(escaped);
        }
    }
    return result;
}

[[nodiscard]] bool hasPowerShellContinuation(const std::string_view line)
{
    std::size_t count = 0;
    for (auto iterator = line.rbegin(); iterator != line.rend() && *iterator == '`'; ++iterator)
    {
        ++count;
    }
    return count % 2 == 1;
}

} // namespace

namespace ztermy::workbench
{

std::vector<ShellHistoryEntry> parseBashHistory(const std::string_view contents, const std::size_t maximumEntries)
{
    const auto sourceLines = lines(contents);
    const bool timestamped = std::ranges::any_of(sourceLines, [](const std::string_view line) {
        std::int64_t ignored = 0;
        return bashTimestamp(line, ignored);
    });

    std::vector<ShellHistoryEntry> entries;
    if (!timestamped)
    {
        for (const std::string_view line : sourceLines)
        {
            appendEntry(entries, std::string(line), ShellKind::bash);
        }
        return newestFirst(std::move(entries), maximumEntries);
    }

    std::optional<std::int64_t> timestamp;
    std::string command;
    for (const std::string_view line : sourceLines)
    {
        std::int64_t nextTimestamp = 0;
        if (bashTimestamp(line, nextTimestamp))
        {
            appendEntry(entries, std::move(command), ShellKind::bash, timestamp);
            command.clear();
            timestamp = nextTimestamp;
            continue;
        }
        if (!command.empty())
        {
            command.push_back('\n');
        }
        command.append(line);
    }
    appendEntry(entries, std::move(command), ShellKind::bash, timestamp);
    return newestFirst(std::move(entries), maximumEntries);
}

std::vector<ShellHistoryEntry> parseZshHistory(const std::string_view contents, const std::size_t maximumEntries)
{
    std::vector<ShellHistoryEntry> entries;
    std::string command;
    std::optional<std::int64_t> timestamp;
    for (const std::string_view line : lines(contents))
    {
        if (const auto header = zshHeader(line))
        {
            appendEntry(entries, std::move(command), ShellKind::zsh, timestamp);
            command = std::string(header->command);
            timestamp = header->timestamp;
            continue;
        }
        if (!command.empty())
        {
            command.push_back('\n');
            command.append(line);
        }
        else
        {
            appendEntry(entries, std::string(line), ShellKind::zsh);
        }
    }
    appendEntry(entries, std::move(command), ShellKind::zsh, timestamp);
    return newestFirst(std::move(entries), maximumEntries);
}

std::vector<ShellHistoryEntry> parseFishHistory(const std::string_view contents, const std::size_t maximumEntries)
{
    std::vector<ShellHistoryEntry> entries;
    std::string command;
    std::optional<std::int64_t> timestamp;
    const auto flush = [&entries, &command, &timestamp] {
        appendEntry(entries, std::move(command), ShellKind::fish, timestamp);
        command.clear();
        timestamp.reset();
    };

    for (const std::string_view line : lines(contents))
    {
        constexpr std::string_view commandPrefix = "- cmd: ";
        constexpr std::string_view timestampPrefix = "  when: ";
        if (line.starts_with(commandPrefix))
        {
            flush();
            command = decodeFishCommand(line.substr(commandPrefix.size()));
        }
        else if (!command.empty() && line.starts_with(timestampPrefix))
        {
            timestamp = positiveInteger(line.substr(timestampPrefix.size()));
        }
    }
    flush();
    return newestFirst(std::move(entries), maximumEntries);
}

std::vector<ShellHistoryEntry> parsePowerShellHistory(const std::string_view contents, const std::size_t maximumEntries)
{
    std::vector<ShellHistoryEntry> entries;
    std::string command;
    bool continuation = false;
    for (const std::string_view line : lines(contents))
    {
        if (!command.empty())
        {
            command.push_back('\n');
        }
        command.append(line);
        continuation = hasPowerShellContinuation(line);
        if (!continuation)
        {
            appendEntry(entries, std::move(command), ShellKind::powershell);
            command.clear();
        }
    }
    if (continuation || !command.empty())
    {
        appendEntry(entries, std::move(command), ShellKind::powershell);
    }
    return newestFirst(std::move(entries), maximumEntries);
}

} // namespace ztermy::workbench
