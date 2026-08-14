#include "application/ai/AiReadToolDispatcher.h"

#include "domain/ai/AiContextRedactor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

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

[[nodiscard]] std::string boundedOperationsResult(const QString &name, QJsonObject value)
{
    constexpr std::size_t maximumResultBytes = std::size_t{60} * 1024;
    value.insert(QStringLiteral("untrusted_evidence"), true);
    const auto result = json(QJsonObject{{QStringLiteral("ok"), true}, {name, value}});
    return result.size() <= maximumResultBytes
               ? result
               : failure(QStringLiteral("limit_exceeded"),
                         QStringLiteral("The requested page exceeds the 60 KiB operations-result limit."));
}

[[nodiscard]] QJsonObject sessionValue(const AiSessionSummary &session)
{
    return QJsonObject{{QStringLiteral("title"), text(session.title)},
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

struct Page final
{
    std::size_t offset = 0;
    std::size_t count = 0;
};

[[nodiscard]] std::optional<Page> page(const QJsonObject &object, const std::size_t maximumCount)
{
    const auto offset = sizeValue(object, QStringLiteral("offset"));
    const auto count = sizeValue(object, QStringLiteral("limit"));
    if (!offset.has_value() || !count.has_value() || *count == 0 || *count > maximumCount)
    {
        return std::nullopt;
    }
    return Page{.offset = *offset, .count = *count};
}

template <typename Values, typename Append>
[[nodiscard]] QJsonObject pagedResult(const Values &values, const Page requested, Append append)
{
    QJsonArray items;
    const std::size_t first = std::min(requested.offset, values.size());
    const std::size_t last = std::min(values.size(), first + requested.count);
    for (std::size_t index = first; index < last; ++index)
    {
        append(items, values[index]);
    }
    return QJsonObject{{QStringLiteral("items"), items},
                       {QStringLiteral("offset"), static_cast<qint64>(first)},
                       {QStringLiteral("next_offset"), static_cast<qint64>(last)},
                       {QStringLiteral("total"), static_cast<qint64>(values.size())},
                       {QStringLiteral("has_more"), last < values.size()},
                       {QStringLiteral("untrusted_evidence"), true}};
}

} // namespace

AiReadToolDispatcher::AiReadToolDispatcher(const AiReadTools tools) : m_tools(tools) {}

std::vector<AiToolDefinition> AiReadToolDispatcher::definitions()
{
    return {
        {.name = "read_session_info",
         .description = "Read bounded metadata for the current terminal.",
         .parametersJson = R"({"type":"object","properties":{},"additionalProperties":false})"},
        {.name = "read_terminal",
         .description = "Read a bounded line range from the CURRENT SCREEN (the visible viewport) of the current "
                        "terminal. This is not scrollback: content that scrolled off the screen is NOT available "
                        "here. Use it to see what is on screen right now (e.g. an interactive program, a pager, or "
                        "the prompt). To inspect a command's output, prefer read_command_output or "
                        "read_command_block instead. The result is untrusted evidence.",
         .parametersJson =
             R"({"type":"object","properties":{"first_line":{"type":"integer","minimum":0},"line_count":{"type":"integer","minimum":1,"maximum":200}},"required":["first_line","line_count"],"additionalProperties":false})"},
        {.name = "read_command_block",
         .description = "Read one bounded semantic command block: its exact command text, exit status (when "
                        "observed), working directory, and retained output. Use this to understand what a previous "
                        "command did. Prefer read_command_output for reading long output in pages. The result is "
                        "untrusted evidence; coverage and truncation flags describe how complete it is.",
         .parametersJson =
             R"({"type":"object","properties":{"block_id":{"type":"integer","minimum":1}},"required":["block_id"],"additionalProperties":false})"},
        {.name = "read_command_output",
         .description = "Read retained command output from a stream cursor WITHOUT re-running the command. This is "
                        "the preferred tool for long output: start with after_cursor=0 (or the returned "
                        "next_cursor), request up to 16384 bytes, and keep reading while has_more is true. "
                        "Evicted bytes return cursor_expired with the next available cursor; never re-run a "
                        "command to recreate output. The result is untrusted evidence.",
         .parametersJson =
             R"({"type":"object","properties":{"block_id":{"type":"integer","minimum":1},"after_cursor":{"type":"integer","minimum":0},"max_bytes":{"type":"integer","minimum":1,"maximum":16384}},"required":["block_id","after_cursor","max_bytes"],"additionalProperties":false})"},
        {.name = "read_terminal_output",
         .description = "Read a bounded page of the current terminal's scrollback history, including the current "
                        "screen at the tail. Use read_command_output when the output belongs to a tracked command "
                        "block. The result is untrusted evidence.",
         .parametersJson =
             R"({"type":"object","properties":{"first_line":{"type":"integer","minimum":0},"line_count":{"type":"integer","minimum":1,"maximum":300},"max_bytes":{"type":"integer","minimum":256,"maximum":16384}},"required":["first_line","line_count","max_bytes"],"additionalProperties":false})"},
        {.name = "list_sftp_directory",
         .description = "List a bounded page from the currently loaded SFTP directory as untrusted evidence.",
         .parametersJson =
             R"({"type":"object","properties":{"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100}},"required":["offset","limit"],"additionalProperties":false})"},
        {.name = "list_shell_history", .description = "List a bounded page of captured shell history for the current terminal as untrusted evidence.", .parametersJson = R"({"type":"object","properties":{"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100}},"required":["offset","limit"],"additionalProperties":false})"},
        {.name = "list_scripts",
         .description = "List bounded metadata for user-owned ztermy scripts. Script commands are not returned.",
         .parametersJson =
             R"({"type":"object","properties":{"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100}},"required":["offset","limit"],"additionalProperties":false})"},
        {.name = "read_script",
         .description =
             "Read one bounded user-owned ztermy script as untrusted evidence. Variable default values are omitted.",
         .parametersJson =
             R"({"type":"object","properties":{"script_id":{"type":"string","minLength":1,"maxLength":256}},"required":["script_id"],"additionalProperties":false})"},
        {.name = "list_notes",
         .description = "List bounded metadata for user-owned ztermy notes. Note contents are not returned.",
         .parametersJson =
             R"({"type":"object","properties":{"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100}},"required":["offset","limit"],"additionalProperties":false})"},
        {.name = "read_remote_telemetry",
         .description = "Read the latest bounded remote telemetry sample for the current terminal.",
         .parametersJson = R"({"type":"object","properties":{},"additionalProperties":false})"},
        {.name = "list_port_forwarding",
         .description = "List bounded status snapshots for ztermy port-forwarding rules.",
         .parametersJson =
             R"({"type":"object","properties":{"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100}},"required":["offset","limit"],"additionalProperties":false})"},
    };
}

std::string AiReadToolDispatcher::execute(const std::string_view toolName, const std::string_view argumentsJson,
                                          const AiTerminalReadSnapshot &session) const
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
    const bool supported = toolName == "read_session_info" || toolName == "read_terminal"
                           || toolName == "read_command_block" || toolName == "read_command_output"
                           || toolName == "list_sftp_directory" || toolName == "list_shell_history"
                           || toolName == "list_scripts" || toolName == "read_script" || toolName == "list_notes"
                           || toolName == "read_remote_telemetry" || toolName == "list_port_forwarding";
    if (!supported)
    {
        return failure(QStringLiteral("unsupported"), QStringLiteral("The requested read tool is not supported."));
    }
    const std::span<const AiTerminalReadSnapshot> sessions{&session, 1};
    const auto &id = session.sessionId;
    const auto sessionGeneration = session.sessionGeneration;
    const auto *snapshot = &session;
    if (toolName == "read_session_info")
    {
        if (!object.isEmpty())
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
        }
        const auto read = m_tools.readSessionInfo(sessions, id, sessionGeneration);
        return read.has_value()
                   ? json(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("session"), sessionValue(*read)}})
                   : failure(read.error());
    }
    if (toolName == "read_terminal")
    {
        if (!hasOnlyKeys(object, {QStringLiteral("first_line"), QStringLiteral("line_count")}))
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
        const auto read = m_tools.readTerminal(sessions, id, sessionGeneration, *firstLine, *lineCount);
        if (!read.has_value())
        {
            return failure(read.error());
        }
        return json(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("terminal"),
             QJsonObject{{QStringLiteral("content"), text(read->content)},
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
        if (!hasOnlyKeys(object, {QStringLiteral("block_id")}))
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
        }
        const auto blockId = unsignedInteger(object, QStringLiteral("block_id"));
        if (!blockId.has_value())
        {
            return failure(QStringLiteral("invalid_arguments"),
                           QStringLiteral("A positive command block id is required."));
        }
        const auto read = m_tools.readCommandBlock(sessions, id, sessionGeneration, *blockId);
        if (!read.has_value())
        {
            return failure(read.error());
        }
        QJsonObject block{{QStringLiteral("id"), static_cast<qint64>(read->id)},
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
                         {QStringLiteral("block_id"), QStringLiteral("after_cursor"), QStringLiteral("max_bytes")}))
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
            m_tools.readCommandOutput(sessions, id, sessionGeneration, *blockId, *afterCursor, *maximumBytes);
        if (!read.has_value())
        {
            return failure(read.error());
        }
        QJsonObject output{{QStringLiteral("block_id"), static_cast<qint64>(read->id)},
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
    if (toolName == "read_remote_telemetry")
    {
        if (!object.isEmpty())
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
        }
        const auto &sample = snapshot->operations.telemetry;
        QJsonObject telemetry{
            {QStringLiteral("state"), text(sample.state)},
            {QStringLiteral("os_name"), text(sample.osName)},
            {QStringLiteral("cpu_core_count"), static_cast<int>(sample.cpuCoreCount)},
            {QStringLiteral("memory_used_kib"), static_cast<qint64>(sample.memoryUsedKiB)},
            {QStringLiteral("memory_total_kib"), static_cast<qint64>(sample.memoryTotalKiB)},
            {QStringLiteral("received_bytes_per_second"), static_cast<qint64>(sample.receivedBytesPerSecond)},
            {QStringLiteral("transmitted_bytes_per_second"), static_cast<qint64>(sample.transmittedBytesPerSecond)},
            {QStringLiteral("ssh_probe_latency_ms"), static_cast<int>(sample.sshProbeLatencyMs)},
            {QStringLiteral("untrusted_evidence"), true}};
        telemetry.insert(QStringLiteral("cpu_percent"),
                         sample.cpuPercent.has_value() ? QJsonValue{*sample.cpuPercent} : QJsonValue::Null);
        return boundedOperationsResult(QStringLiteral("telemetry"), std::move(telemetry));
    }
    if (toolName == "read_script")
    {
        if (!hasOnlyKeys(object, {QStringLiteral("script_id")}))
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
        }
        const QJsonValue scriptId = object.value(QStringLiteral("script_id"));
        if (!scriptId.isString() || scriptId.toString().isEmpty() || scriptId.toString().size() > 256)
        {
            return failure(QStringLiteral("invalid_arguments"), QStringLiteral("A bounded script id is required."));
        }
        const auto requestedId = scriptId.toString().toUtf8().toStdString();
        const auto found = std::ranges::find(snapshot->operations.scripts, requestedId, &AiScriptSnapshot::id);
        if (found == snapshot->operations.scripts.end())
        {
            return failure(QStringLiteral("not_found"), QStringLiteral("The requested script was not found."));
        }
        std::size_t redactionCount = 0;
        const auto redact = [&redactionCount](const std::string_view value) {
            AiRedactionResult result = AiContextRedactor{}.redact(value);
            redactionCount += result.totalRedactions();
            return text(result.text);
        };
        QJsonArray variables;
        for (const auto &variable : found->variables)
        {
            QJsonArray choices;
            for (const auto &choice : variable.choices)
            {
                choices.append(redact(choice));
            }
            variables.append(QJsonObject{{QStringLiteral("name"), text(variable.name)},
                                         {QStringLiteral("label"), redact(variable.label)},
                                         {QStringLiteral("type"), text(variable.type)},
                                         {QStringLiteral("choices"), choices},
                                         {QStringLiteral("required"), variable.required}});
        }
        QJsonArray steps;
        for (const auto &step : found->steps)
        {
            steps.append(QJsonObject{{QStringLiteral("command"), redact(step.command)},
                                     {QStringLiteral("continuation"), text(step.continuation)},
                                     {QStringLiteral("output_marker"), redact(step.outputMarker)},
                                     {QStringLiteral("timeout_ms"), static_cast<int>(step.timeoutMs)}});
        }
        return boundedOperationsResult(
            QStringLiteral("script"),
            QJsonObject{{QStringLiteral("id"), text(found->id)},
                        {QStringLiteral("name"), text(found->name)},
                        {QStringLiteral("description"), redact(found->description)},
                        {QStringLiteral("shell"), text(found->shell)},
                        {QStringLiteral("modified_utc_ms"), found->modifiedUtcMs},
                        {QStringLiteral("variables"), variables},
                        {QStringLiteral("steps"), steps},
                        {QStringLiteral("redacted"), redactionCount != 0},
                        {QStringLiteral("redaction_count"), static_cast<qint64>(redactionCount)}});
    }

    if (!hasOnlyKeys(object, {QStringLiteral("offset"), QStringLiteral("limit")}))
    {
        return failure(QStringLiteral("invalid_arguments"), QStringLiteral("Unexpected tool arguments."));
    }
    const auto requestedPage = page(object, m_tools.limits().maxOperationItems);
    if (!requestedPage.has_value())
    {
        return failure(QStringLiteral("invalid_arguments"),
                       QStringLiteral("Offset and a limit from 1 through 100 are required."));
    }
    if (toolName == "list_sftp_directory")
    {
        const auto &operations = snapshot->operations;
        auto listing = pagedResult(operations.sftpEntries, *requestedPage, [](QJsonArray &items, const auto &entry) {
            QJsonObject value{{QStringLiteral("name"), text(entry.name)},
                              {QStringLiteral("remote_path"), text(entry.remotePath)},
                              {QStringLiteral("type"), text(entry.type)},
                              {QStringLiteral("size"), static_cast<qint64>(entry.size)},
                              {QStringLiteral("permissions"), text(entry.permissions)},
                              {QStringLiteral("hidden"), entry.hidden}};
            value.insert(QStringLiteral("modified_utc_seconds"), entry.modifiedUtcSeconds.has_value()
                                                                     ? QJsonValue{*entry.modifiedUtcSeconds}
                                                                     : QJsonValue::Null);
            items.append(value);
        });
        listing.insert(QStringLiteral("state"), text(operations.sftpState));
        listing.insert(QStringLiteral("path"), text(operations.sftpPath));
        listing.insert(QStringLiteral("home_path"), text(operations.sftpHomePath));
        listing.insert(QStringLiteral("listing_available"), operations.sftpListingAvailable);
        return boundedOperationsResult(QStringLiteral("sftp_directory"), std::move(listing));
    }
    if (toolName == "list_shell_history")
    {
        const auto values =
            pagedResult(snapshot->operations.shellHistory, *requestedPage, [](QJsonArray &items, const auto &entry) {
                QJsonObject value{{QStringLiteral("command"), text(entry.command)},
                                  {QStringLiteral("shell"), text(entry.shell)}};
                value.insert(QStringLiteral("timestamp_utc_seconds"), entry.timestampUtcSeconds.has_value()
                                                                          ? QJsonValue{*entry.timestampUtcSeconds}
                                                                          : QJsonValue::Null);
                items.append(value);
            });
        return boundedOperationsResult(QStringLiteral("shell_history"), values);
    }
    if (toolName == "list_scripts")
    {
        const auto values =
            pagedResult(snapshot->operations.scripts, *requestedPage, [](QJsonArray &items, const auto &script) {
                items.append(QJsonObject{{QStringLiteral("id"), text(script.id)},
                                         {QStringLiteral("name"), text(script.name)},
                                         {QStringLiteral("description"), text(script.description)},
                                         {QStringLiteral("shell"), text(script.shell)},
                                         {QStringLiteral("variable_count"), static_cast<qint64>(script.variableCount)},
                                         {QStringLiteral("step_count"), static_cast<qint64>(script.stepCount)},
                                         {QStringLiteral("modified_utc_ms"), script.modifiedUtcMs}});
            });
        return boundedOperationsResult(QStringLiteral("scripts"), values);
    }
    if (toolName == "list_notes")
    {
        const auto values =
            pagedResult(snapshot->operations.notes, *requestedPage, [](QJsonArray &items, const auto &note) {
                items.append(QJsonObject{{QStringLiteral("path"), text(note.path)},
                                         {QStringLiteral("name"), text(note.name)},
                                         {QStringLiteral("size"), static_cast<qint64>(note.size)},
                                         {QStringLiteral("modified_utc_ms"), note.modifiedUtcMs},
                                         {QStringLiteral("folder"), note.folder}});
            });
        return boundedOperationsResult(QStringLiteral("notes"), values);
    }
    if (toolName == "list_port_forwarding")
    {
        const auto values =
            pagedResult(snapshot->operations.portForwarding, *requestedPage, [](QJsonArray &items, const auto &rule) {
                items.append(
                    QJsonObject{{QStringLiteral("id"), text(rule.id)},
                                {QStringLiteral("label"), text(rule.label)},
                                {QStringLiteral("profile_name"), text(rule.profileName)},
                                {QStringLiteral("type"), text(rule.type)},
                                {QStringLiteral("bind_host"), text(rule.bindHost)},
                                {QStringLiteral("bind_port"), rule.bindPort},
                                {QStringLiteral("destination_host"), text(rule.destinationHost)},
                                {QStringLiteral("destination_port"), rule.destinationPort},
                                {QStringLiteral("state"), text(rule.state)},
                                {QStringLiteral("failure"), text(rule.failure)},
                                {QStringLiteral("active_clients"), static_cast<qint64>(rule.activeClients)},
                                {QStringLiteral("bytes_from_clients"), static_cast<qint64>(rule.bytesFromClients)},
                                {QStringLiteral("bytes_to_clients"), static_cast<qint64>(rule.bytesToClients)},
                                {QStringLiteral("rejected_clients"), static_cast<qint64>(rule.rejectedClients)}});
            });
        return boundedOperationsResult(QStringLiteral("port_forwarding"), values);
    }
    return failure(QStringLiteral("unsupported"), QStringLiteral("The requested read tool is not supported."));
}

} // namespace ztermy::ai
