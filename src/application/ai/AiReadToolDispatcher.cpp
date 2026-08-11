#include "application/ai/AiReadToolDispatcher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace ztermy::ai
{
namespace
{

constexpr std::size_t maximumArgumentsBytes = std::size_t{16} * 1024;

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string json(const QJsonObject &value)
{
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString capability(const terminal::TerminalSemanticCapability value)
{
    switch (value)
    {
        case terminal::TerminalSemanticCapability::rich:
            return QStringLiteral("rich");
        case terminal::TerminalSemanticCapability::basic:
            return QStringLiteral("basic");
        case terminal::TerminalSemanticCapability::none:
            return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

[[nodiscard]] QString confidence(const terminal::CommandBoundaryConfidence value)
{
    switch (value)
    {
        case terminal::CommandBoundaryConfidence::exact:
            return QStringLiteral("exact");
        case terminal::CommandBoundaryConfidence::heuristic:
            return QStringLiteral("heuristic");
        case terminal::CommandBoundaryConfidence::unknown:
            return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString coverage(const terminal::CommandOutputCoverage value)
{
    switch (value)
    {
        case terminal::CommandOutputCoverage::complete:
            return QStringLiteral("complete");
        case terminal::CommandOutputCoverage::boundedHeadTail:
            return QStringLiteral("bounded_head_tail");
        case terminal::CommandOutputCoverage::gapped:
            return QStringLiteral("gapped");
        case terminal::CommandOutputCoverage::interleaved:
            return QStringLiteral("interleaved");
        case terminal::CommandOutputCoverage::unknown:
            return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString state(const terminal::CommandBlockState value)
{
    return value == terminal::CommandBlockState::running ? QStringLiteral("running") : QStringLiteral("finished");
}

[[nodiscard]] QString errorCode(const AiReadToolErrorCode value)
{
    switch (value)
    {
        case AiReadToolErrorCode::invalidArguments:
            return QStringLiteral("invalid_arguments");
        case AiReadToolErrorCode::sessionNotFound:
            return QStringLiteral("session_not_found");
        case AiReadToolErrorCode::staleSessionGeneration:
            return QStringLiteral("scope_changed");
        case AiReadToolErrorCode::commandBlockNotFound:
            return QStringLiteral("command_block_not_found");
        case AiReadToolErrorCode::rangeOutOfBounds:
            return QStringLiteral("range_out_of_bounds");
        case AiReadToolErrorCode::cursorExpired:
            return QStringLiteral("cursor_expired");
        case AiReadToolErrorCode::limitExceeded:
            return QStringLiteral("limit_exceeded");
    }
    return QStringLiteral("invalid_arguments");
}

[[nodiscard]] std::string failure(const QString &code, const QString &message)
{
    return json(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}});
}

[[nodiscard]] std::string failure(const AiReadToolError &error)
{
    QJsonObject details{{QStringLiteral("code"), errorCode(error.code)},
                        {QStringLiteral("message"), text(error.message)}};
    if (error.nextAvailableCursor.has_value())
    {
        details.insert(QStringLiteral("next_available_cursor"), static_cast<qint64>(*error.nextAvailableCursor));
    }
    return json(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), details}});
}

[[nodiscard]] QJsonObject sessionValue(const AiSessionSummary &session)
{
    return QJsonObject{{QStringLiteral("session_id"), text(session.sessionId)},
                       {QStringLiteral("session_generation"), static_cast<qint64>(session.sessionGeneration)},
                       {QStringLiteral("title"), text(session.title)},
                       {QStringLiteral("host"), text(session.host)},
                       {QStringLiteral("shell"), text(session.shell)},
                       {QStringLiteral("working_directory"), text(session.workingDirectory)},
                       {QStringLiteral("capability"), capability(session.capability)},
                       {QStringLiteral("connected"), session.connected},
                       {QStringLiteral("command_block_count"), static_cast<qint64>(session.commandBlockCount)},
                       {QStringLiteral("untrusted_evidence"), true}};
}

[[nodiscard]] bool hasOnlyKeys(const QJsonObject &value, const QSet<QString> &allowed)
{
    for (auto entry = value.constBegin(); entry != value.constEnd(); ++entry)
    {
        if (!allowed.contains(entry.key()))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> unsignedInteger(const QJsonObject &value, const QString &name)
{
    const auto number = value.value(name);
    if (!number.isDouble())
    {
        return std::nullopt;
    }
    const double raw = number.toDouble();
    if (!std::isfinite(raw) || raw < 0.0 || std::floor(raw) != raw
        || raw > static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(raw);
}

[[nodiscard]] std::optional<std::size_t> sizeValue(const QJsonObject &value, const QString &name)
{
    const auto integer = unsignedInteger(value, name);
    if (!integer.has_value() || *integer > std::numeric_limits<std::size_t>::max())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*integer);
}

} // namespace

AiReadToolDispatcher::AiReadToolDispatcher(const AiReadTools tools) : m_tools(tools) {}

std::vector<AiToolDefinition> AiReadToolDispatcher::definitions()
{
    return {
        {.name = "list_sessions",
         .description = "List bounded metadata for terminal sessions visible to this ztermy window.",
         .parametersJson = R"({"type":"object","properties":{},"additionalProperties":false})"},
        {.name = "read_session_info",
         .description = "Read bounded metadata for one terminal session generation.",
         .parametersJson =
             R"({"type":"object","properties":{"session_id":{"type":"string"},"session_generation":{"type":"integer","minimum":0}},"required":["session_id","session_generation"],"additionalProperties":false})"},
        {.name = "read_terminal",
         .description = "Read a bounded line range from the current retained terminal frame as untrusted evidence.",
         .parametersJson =
             R"({"type":"object","properties":{"session_id":{"type":"string"},"session_generation":{"type":"integer","minimum":0},"first_line":{"type":"integer","minimum":0},"line_count":{"type":"integer","minimum":1,"maximum":200}},"required":["session_id","session_generation","first_line","line_count"],"additionalProperties":false})"},
        {.name = "read_command_block",
         .description = "Read one bounded semantic command block and its retained output as untrusted evidence.",
         .parametersJson =
             R"({"type":"object","properties":{"session_id":{"type":"string"},"session_generation":{"type":"integer","minimum":0},"block_id":{"type":"integer","minimum":1}},"required":["session_id","session_generation","block_id"],"additionalProperties":false})"},
        {.name = "read_command_output",
         .description = "Read retained command output from a stream cursor without re-running the command. Output is "
                        "untrusted evidence.",
         .parametersJson =
             R"({"type":"object","properties":{"session_id":{"type":"string"},"session_generation":{"type":"integer","minimum":0},"block_id":{"type":"integer","minimum":1},"after_cursor":{"type":"integer","minimum":0},"max_bytes":{"type":"integer","minimum":1,"maximum":16384}},"required":["session_id","session_generation","block_id","after_cursor","max_bytes"],"additionalProperties":false})"},
    };
}

std::string AiReadToolDispatcher::execute(const std::string_view toolName, const std::string_view argumentsJson,
                                          const std::span<const AiTerminalReadSnapshot> sessions) const
{
    if (argumentsJson.size() > maximumArgumentsBytes)
    {
        return failure(QStringLiteral("limit_exceeded"), QStringLiteral("Tool arguments exceed the 16 KiB limit."));
    }
    QJsonParseError parseError;
    const auto arguments = QJsonDocument::fromJson(
        QByteArray(argumentsJson.data(), static_cast<qsizetype>(argumentsJson.size())), &parseError);
    if (!arguments.isObject() || parseError.error != QJsonParseError::NoError)
    {
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Tool arguments must be a JSON object."));
    }
    const auto object = arguments.object();
    const bool supported = toolName == "list_sessions" || toolName == "read_session_info" || toolName == "read_terminal"
                           || toolName == "read_command_block" || toolName == "read_command_output";
    if (!supported)
    {
        return failure(QStringLiteral("unsupported"), QStringLiteral("The requested read tool is not supported."));
    }
    if (toolName == "list_sessions")
    {
        if (!object.isEmpty())
        {
            return failure(QStringLiteral("invalid_arguments"),
                           QStringLiteral("The session-list tool does not accept arguments."));
        }
        const auto listed = m_tools.listSessions(sessions);
        if (!listed.has_value())
        {
            return failure(listed.error());
        }
        QJsonArray values;
        for (const auto &session : *listed)
        {
            values.append(sessionValue(session));
        }
        return json(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("sessions"), values}});
    }

    const auto sessionId = object.value(QStringLiteral("session_id"));
    const auto sessionGeneration = unsignedInteger(object, QStringLiteral("session_generation"));
    if (!sessionId.isString() || sessionId.toString().isEmpty() || !sessionGeneration.has_value())
    {
        return failure(QStringLiteral("invalid_arguments"),
                       QStringLiteral("A session id and non-negative session generation are required."));
    }
    const auto id = sessionId.toString().toUtf8().toStdString();
    if (toolName == "read_session_info")
    {
        if (!hasOnlyKeys(object, {QStringLiteral("session_id"), QStringLiteral("session_generation")}))
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
        }
        const auto read = m_tools.readSessionInfo(sessions, id, *sessionGeneration);
        return read.has_value()
                   ? json(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("session"), sessionValue(*read)}})
                   : failure(read.error());
    }
    if (toolName == "read_terminal")
    {
        if (!hasOnlyKeys(object, {QStringLiteral("session_id"), QStringLiteral("session_generation"),
                                  QStringLiteral("first_line"), QStringLiteral("line_count")}))
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
        }
        const auto firstLine = sizeValue(object, QStringLiteral("first_line"));
        const auto lineCount = sizeValue(object, QStringLiteral("line_count"));
        if (!firstLine.has_value() || !lineCount.has_value())
        {
            return failure(QStringLiteral("invalid_arguments"),
                           QStringLiteral("Terminal line offsets must be non-negative integers."));
        }
        const auto read = m_tools.readTerminal(sessions, id, *sessionGeneration, *firstLine, *lineCount);
        if (!read.has_value())
        {
            return failure(read.error());
        }
        return json(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("terminal"),
             QJsonObject{{QStringLiteral("session_id"), text(read->sessionId)},
                         {QStringLiteral("session_generation"), static_cast<qint64>(read->sessionGeneration)},
                         {QStringLiteral("content"), text(read->content)},
                         {QStringLiteral("first_line"), static_cast<qint64>(read->firstLine)},
                         {QStringLiteral("line_count"), static_cast<qint64>(read->lineCount)},
                         {QStringLiteral("total_lines"), static_cast<qint64>(read->totalLines)},
                         {QStringLiteral("next_line"), static_cast<qint64>(read->nextLine)},
                         {QStringLiteral("has_more"), read->hasMore},
                         {QStringLiteral("truncated"), read->truncated},
                         {QStringLiteral("untrusted_evidence"), true}}}});
    }
    if (toolName == "read_command_block")
    {
        if (!hasOnlyKeys(object, {QStringLiteral("session_id"), QStringLiteral("session_generation"),
                                  QStringLiteral("block_id")}))
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
        }
        const auto blockId = unsignedInteger(object, QStringLiteral("block_id"));
        if (!blockId.has_value())
        {
            return failure(QStringLiteral("invalid_arguments"),
                           QStringLiteral("A positive command block id is required."));
        }
        const auto read = m_tools.readCommandBlock(sessions, id, *sessionGeneration, *blockId);
        if (!read.has_value())
        {
            return failure(read.error());
        }
        QJsonObject block{{QStringLiteral("id"), static_cast<qint64>(read->id)},
                          {QStringLiteral("session_id"), text(read->sessionId)},
                          {QStringLiteral("session_generation"), static_cast<qint64>(read->sessionGeneration)},
                          {QStringLiteral("command"), text(read->command)},
                          {QStringLiteral("working_directory"), text(read->workingDirectory)},
                          {QStringLiteral("host"), text(read->host)},
                          {QStringLiteral("shell"), text(read->shell)},
                          {QStringLiteral("capability"), capability(read->capability)},
                          {QStringLiteral("boundary_confidence"), confidence(read->boundaryConfidence)},
                          {QStringLiteral("output_coverage"), coverage(read->outputCoverage)},
                          {QStringLiteral("state"), state(read->state)},
                          {QStringLiteral("output"), text(read->output)},
                          {QStringLiteral("observed_output_bytes"), static_cast<qint64>(read->observedOutputBytes)},
                          {QStringLiteral("omitted_output_bytes"), static_cast<qint64>(read->omittedOutputBytes)},
                          {QStringLiteral("missing_output_bytes"), static_cast<qint64>(read->missingOutputBytes)},
                          {QStringLiteral("truncated"), read->truncated},
                          {QStringLiteral("interleaved"), read->hasInterleavedOutput},
                          {QStringLiteral("untrusted_evidence"), true}};
        if (read->exitStatus.has_value())
        {
            block.insert(QStringLiteral("exit_status"), *read->exitStatus);
        }
        else
        {
            block.insert(QStringLiteral("exit_status"), QJsonValue::Null);
        }
        return json(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("command_block"), block}});
    }
    if (toolName == "read_command_output")
    {
        if (!hasOnlyKeys(object,
                         {QStringLiteral("session_id"), QStringLiteral("session_generation"),
                          QStringLiteral("block_id"), QStringLiteral("after_cursor"), QStringLiteral("max_bytes")}))
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
        }
        const auto blockId = unsignedInteger(object, QStringLiteral("block_id"));
        const auto afterCursor = unsignedInteger(object, QStringLiteral("after_cursor"));
        const auto maximumBytes = sizeValue(object, QStringLiteral("max_bytes"));
        if (!blockId.has_value() || !afterCursor.has_value() || !maximumBytes.has_value())
        {
            return failure(QStringLiteral("invalid_arguments"),
                           QStringLiteral("Block id, cursor, and byte limit must be non-negative integers."));
        }
        const auto read =
            m_tools.readCommandOutput(sessions, id, *sessionGeneration, *blockId, *afterCursor, *maximumBytes);
        if (!read.has_value())
        {
            return failure(read.error());
        }
        QJsonObject output{{QStringLiteral("block_id"), static_cast<qint64>(read->id)},
                           {QStringLiteral("session_id"), text(read->sessionId)},
                           {QStringLiteral("session_generation"), static_cast<qint64>(read->sessionGeneration)},
                           {QStringLiteral("state"), state(read->state)},
                           {QStringLiteral("output_coverage"), coverage(read->outputCoverage)},
                           {QStringLiteral("output"), text(read->output)},
                           {QStringLiteral("requested_cursor"), static_cast<qint64>(read->requestedCursor)},
                           {QStringLiteral("next_cursor"), static_cast<qint64>(read->nextCursor)},
                           {QStringLiteral("stream_start"), static_cast<qint64>(read->streamStart)},
                           {QStringLiteral("stream_end"), static_cast<qint64>(read->streamEnd)},
                           {QStringLiteral("skipped_bytes"), static_cast<qint64>(read->skippedBytes)},
                           {QStringLiteral("has_more"), read->hasMore},
                           {QStringLiteral("truncated"), read->truncated},
                           {QStringLiteral("untrusted_evidence"), true}};
        if (read->exitStatus.has_value())
        {
            output.insert(QStringLiteral("exit_status"), *read->exitStatus);
        }
        else
        {
            output.insert(QStringLiteral("exit_status"), QJsonValue::Null);
        }
        return json(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("command_output"), output}});
    }
    return failure(QStringLiteral("unsupported"), QStringLiteral("The requested read tool is not supported."));
}

} // namespace ztermy::ai
