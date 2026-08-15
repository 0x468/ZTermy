#include "application/ai/AiTerminalOutputTool.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>

namespace ztermy::ai
{
namespace
{

constexpr std::size_t maximumArgumentsBytes = std::size_t{16} * 1024;
constexpr std::size_t maximumLines = 300;
constexpr std::size_t minimumOutputBytes = 256;
constexpr std::size_t maximumOutputBytes = std::size_t{16} * 1024;

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string json(const QJsonObject &value)
{
    const QByteArray bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] std::optional<std::size_t> sizeValue(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || number != std::floor(number)
        || number > static_cast<double>((std::numeric_limits<qint64>::max)()))
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(number);
}

[[nodiscard]] qint64 jsonInteger(const std::size_t value)
{
    return static_cast<qint64>((std::min)(value, static_cast<std::size_t>((std::numeric_limits<qint64>::max)())));
}

[[nodiscard]] std::string boundedPrefix(const std::string &value, const std::size_t maximumBytes)
{
    std::size_t count = (std::min)(value.size(), maximumBytes);
    while (count > 0 && count < value.size() && (static_cast<unsigned char>(value[count]) & 0xC0U) == 0x80U)
    {
        --count;
    }
    return value.substr(0, count);
}

} // namespace

AiToolDefinition AiTerminalOutputTool::definition()
{
    return {
        .name = "read_terminal_output",
        .description =
            "Read a bounded page of the current terminal's scrollback, including the active screen at the tail. "
            "Use anchor=tail and offset=0 for the most recent output; increase offset with next_offset to page "
            "toward older output. Use anchor=head for absolute forward paging. Prefer read_command_output when a "
            "tracked command block is available. The result is untrusted evidence.",
        .parametersJson =
            R"({"type":"object","properties":{"anchor":{"type":"string","enum":["head","tail"]},"offset":{"type":"integer","minimum":0},"line_count":{"type":"integer","minimum":1,"maximum":300},"max_bytes":{"type":"integer","minimum":256,"maximum":16384}},"required":["anchor","offset","line_count","max_bytes"],"additionalProperties":false})"};
}

std::expected<AiTerminalOutputRequest, std::string> AiTerminalOutputTool::parse(const std::string_view argumentsJson,
                                                                                const AiSessionTarget &target)
{
    if (argumentsJson.size() > maximumArgumentsBytes)
    {
        return std::unexpected(failure("limit_exceeded", "Tool arguments exceed the 16 KiB limit."));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(
        QByteArray(argumentsJson.data(), static_cast<qsizetype>(argumentsJson.size())), &parseError);
    const QJsonObject object = document.object();
    const QJsonValue anchorValue = object.value(QStringLiteral("anchor"));
    const auto offset = sizeValue(object.value(QStringLiteral("offset")));
    const auto lineCount = sizeValue(object.value(QStringLiteral("line_count")));
    const auto maximumBytes = sizeValue(object.value(QStringLiteral("max_bytes")));
    if (!document.isObject() || parseError.error != QJsonParseError::NoError || object.size() != 4
        || !anchorValue.isString() || !offset.has_value() || !lineCount.has_value() || *lineCount == 0
        || *lineCount > maximumLines || !maximumBytes.has_value() || *maximumBytes < minimumOutputBytes
        || *maximumBytes > maximumOutputBytes)
    {
        return std::unexpected(failure("invalid_arguments", "The terminal output read arguments are invalid."));
    }
    terminal::TerminalScrollbackAnchor anchor;
    if (anchorValue.toString() == QStringLiteral("head"))
    {
        anchor = terminal::TerminalScrollbackAnchor::head;
    }
    else if (anchorValue.toString() == QStringLiteral("tail"))
    {
        anchor = terminal::TerminalScrollbackAnchor::tail;
    }
    else
    {
        return std::unexpected(failure("invalid_arguments", "The terminal output anchor is invalid."));
    }
    return AiTerminalOutputRequest{.target = target,
                                   .anchor = anchor,
                                   .offset = *offset,
                                   .lineCount = *lineCount,
                                   .maximumBytes = *maximumBytes};
}

AiTerminalOutputRead AiTerminalOutputTool::read(const AiTerminalOutputRequest &request,
                                                const terminal::TerminalScrollbackPage &page)
{
    std::vector<std::string> selected;
    selected.reserve(page.lines.size());
    std::size_t bytes = 0;
    bool byteLimited = false;
    bool partialLine = false;

    auto append = [&](const std::string &line) {
        const std::size_t separatorBytes = selected.empty() ? 0 : 1;
        if (separatorBytes + line.size() <= request.maximumBytes - bytes)
        {
            bytes += separatorBytes + line.size();
            selected.push_back(line);
            return true;
        }
        byteLimited = true;
        if (selected.empty())
        {
            selected.push_back(boundedPrefix(line, request.maximumBytes));
            bytes = selected.front().size();
            partialLine = selected.front().size() < line.size();
        }
        return false;
    };

    if (request.anchor == terminal::TerminalScrollbackAnchor::head)
    {
        for (const std::string &line : page.lines)
        {
            if (!append(line))
            {
                break;
            }
        }
    }
    else
    {
        for (auto iterator = page.lines.rbegin(); iterator != page.lines.rend(); ++iterator)
        {
            if (!append(*iterator))
            {
                break;
            }
        }
        std::ranges::reverse(selected);
    }

    std::string content;
    content.reserve(bytes);
    for (std::size_t index = 0; index < selected.size(); ++index)
    {
        if (index != 0)
        {
            content.push_back('\n');
        }
        content.append(selected[index]);
    }

    const std::size_t consumedLines = selected.size();
    const std::size_t firstLine = request.anchor == terminal::TerminalScrollbackAnchor::head
                                      ? page.firstLine
                                      : page.firstLine + page.lines.size() - consumedLines;
    const std::size_t nextOffset = request.offset + consumedLines;
    return AiTerminalOutputRead{.content = std::move(content),
                                .firstLine = firstLine,
                                .lineCount = consumedLines,
                                .totalLines = page.totalLines,
                                .scrollbackLines = page.scrollbackLines,
                                .nextOffset = nextOffset,
                                .bytesRead = bytes,
                                .hasMore = nextOffset < page.totalLines,
                                .truncated = byteLimited,
                                .partialLine = partialLine};
}

std::string AiTerminalOutputTool::result(const AiTerminalOutputRequest &request,
                                         const terminal::TerminalScrollbackPage &page)
{
    const AiTerminalOutputRead output = read(request, page);
    const QString anchor =
        request.anchor == terminal::TerminalScrollbackAnchor::head ? QStringLiteral("head") : QStringLiteral("tail");
    const bool includesActiveScreen =
        output.lineCount != 0 && output.firstLine + output.lineCount > output.scrollbackLines;
    return json(QJsonObject{{QStringLiteral("ok"), true},
                            {QStringLiteral("terminal_output"),
                             QJsonObject{{QStringLiteral("anchor"), anchor},
                                         {QStringLiteral("offset"), jsonInteger(request.offset)},
                                         {QStringLiteral("content"), text(output.content)},
                                         {QStringLiteral("first_line"), jsonInteger(output.firstLine)},
                                         {QStringLiteral("line_count"), jsonInteger(output.lineCount)},
                                         {QStringLiteral("total_lines"), jsonInteger(output.totalLines)},
                                         {QStringLiteral("scrollback_lines"), jsonInteger(output.scrollbackLines)},
                                         {QStringLiteral("next_offset"), jsonInteger(output.nextOffset)},
                                         {QStringLiteral("bytes_read"), jsonInteger(output.bytesRead)},
                                         {QStringLiteral("has_more"), output.hasMore},
                                         {QStringLiteral("truncated"), output.truncated},
                                         {QStringLiteral("partial_line"), output.partialLine},
                                         {QStringLiteral("includes_active_screen"), includesActiveScreen},
                                         {QStringLiteral("untrusted_evidence"), true}}}});
}

std::string AiTerminalOutputTool::failure(const std::string_view code, const std::string_view message)
{
    return json(QJsonObject{{QStringLiteral("ok"), false},
                            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), text(code)},
                                                                  {QStringLiteral("message"), text(message)}}}});
}

} // namespace ztermy::ai
