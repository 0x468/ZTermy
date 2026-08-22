#include "application/ai/AiWaitCommandTool.h"
#include "domain/ai/AiContextBroker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <cmath>
#include <limits>
#include <optional>

namespace ztermy::ai
{
namespace
{

constexpr std::size_t maximumArgumentsBytes = std::size_t{16} * 1024;
constexpr std::uint32_t maximumTimeoutMilliseconds = 600'000;

[[nodiscard]] std::string json(const QJsonObject &value)
{
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
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

[[nodiscard]] std::optional<std::uint64_t> unsignedInteger(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(number);
}

[[nodiscard]] QString state(const AiTrackedCommandState value)
{
    switch (value)
    {
        case AiTrackedCommandState::queued:
            return QStringLiteral("queued");
        case AiTrackedCommandState::running:
            return QStringLiteral("running");
        case AiTrackedCommandState::finished:
            return QStringLiteral("finished");
        case AiTrackedCommandState::disconnected:
            return QStringLiteral("disconnected");
        case AiTrackedCommandState::outcomeUnknown:
            return QStringLiteral("outcome_unknown");
    }
    return QStringLiteral("outcome_unknown");
}

[[nodiscard]] QJsonObject commandValue(const AiTrackedCommand &command, const bool includeOutput)
{
    QJsonObject value{{QStringLiteral("command_id"), text(command.id)},
                      {QStringLiteral("state"), state(command.state)},
                      {QStringLiteral("interrupt_requested"), command.interruptRequested}};
    if (command.blockId.has_value())
    {
        value.insert(QStringLiteral("block_id"), static_cast<qint64>(*command.blockId));
    }
    else
    {
        value.insert(QStringLiteral("block_id"), QJsonValue::Null);
    }
    if (command.exitStatus.has_value())
    {
        value.insert(QStringLiteral("exit_status"), *command.exitStatus);
    }
    else
    {
        value.insert(QStringLiteral("exit_status"), QJsonValue::Null);
    }
    if (includeOutput)
    {
        // Cap the embedded output so the serialized result stays below the
        // AiTurnRunner tool-output bound (64 KiB after JSON escaping). Without
        // this cap a large retained output could hang the turn forever.
        constexpr std::size_t maximumOutputBytes = std::size_t{24} * 1024;
        auto normalized = AiContextBroker::normalizeTerminalText(command.output);
        const bool outputTruncated = normalized.size() > maximumOutputBytes;
        if (outputTruncated)
        {
            auto count = maximumOutputBytes;
            while (count > 0 && count < normalized.size()
                   && (static_cast<unsigned char>(normalized[count]) & 0xC0U) == 0x80U)
            {
                --count;
            }
            normalized.resize(count);
        }
        value.insert(QStringLiteral("output"), text(normalized));
        value.insert(QStringLiteral("output_complete"),
                     command.state == AiTrackedCommandState::finished && !outputTruncated
                         && command.outputCoverage == terminal::CommandOutputCoverage::complete
                         && command.omittedOutputBytes == 0);
        value.insert(QStringLiteral("output_truncated"), outputTruncated);
        value.insert(QStringLiteral("omitted_output_bytes"), static_cast<qint64>(command.omittedOutputBytes));
    }
    return value;
}

[[nodiscard]] QJsonValue commandSucceeded(const AiTrackedCommand &command)
{
    if (command.state != AiTrackedCommandState::finished || !command.exitStatus.has_value())
    {
        return QJsonValue::Null;
    }
    return *command.exitStatus == 0;
}

} // namespace

AiToolDefinition AiWaitCommandTool::definition()
{
    return {
        .name = "wait_command",
        .description = "Wait for a semantic command lifecycle change. Use only when run_command reports "
                       "lifecycle_tracked=true; otherwise use wait_terminal_frame and read_terminal_frame.",
        .parametersJson =
            R"({"type":"object","properties":{"command_id":{"type":"string","minLength":1},"timeout_ms":{"type":"integer","minimum":1,"maximum":600000,"default":30000}},"required":["command_id"],"additionalProperties":false})"};
}

bool AiWaitCommandTool::supportsLifecycleWait(const terminal::TerminalSemanticCapability capability) noexcept
{
    return capability != terminal::TerminalSemanticCapability::none;
}

std::string AiWaitCommandTool::accepted(const std::string_view commandId, const bool trackingRegistered,
                                        const terminal::TerminalSemanticCapability capability,
                                        const std::uint64_t frameRevision)
{
    QString quality = QStringLiteral("unavailable");
    if (capability == terminal::TerminalSemanticCapability::rich)
    {
        quality = QStringLiteral("rich_verified");
    }
    else if (capability == terminal::TerminalSemanticCapability::basic)
    {
        quality = QStringLiteral("basic_unverified");
    }
    const bool lifecycleTracked = trackingRegistered && supportsLifecycleWait(capability);
    QJsonObject value{{QStringLiteral("ok"), true},
                      {QStringLiteral("tool_ok"), true},
                      {QStringLiteral("status"), QStringLiteral("accepted")},
                      {QStringLiteral("command_id"), text(commandId)},
                      {QStringLiteral("command_started"), true},
                      {QStringLiteral("command_completed"), false},
                      {QStringLiteral("command_succeeded"), QJsonValue::Null},
                      {QStringLiteral("tracking_registered"), trackingRegistered},
                      {QStringLiteral("lifecycle_tracked"), lifecycleTracked},
                      {QStringLiteral("lifecycle_quality"), quality},
                      {QStringLiteral("frame_revision_before_dispatch"), static_cast<qint64>(frameRevision)},
                      {QStringLiteral("recommended_wait_tool"),
                       lifecycleTracked ? QStringLiteral("wait_command") : QStringLiteral("wait_terminal_frame")},
                      {QStringLiteral("completion_confirmed"), false}};
    if (!lifecycleTracked)
    {
        value.insert(QStringLiteral("frame_wait_strategy"), QStringLiteral("changed_then_idle"));
        value.insert(QStringLiteral("recommended_idle_ms"), 750);
    }
    return json(value);
}

std::expected<AiWaitCommandRequest, std::string> AiWaitCommandTool::parse(const std::string_view argumentsJson,
                                                                          const AiSessionTarget &target)
{
    if (argumentsJson.size() > maximumArgumentsBytes)
    {
        return std::unexpected(failure("limit_exceeded", "Tool arguments exceed the 16 KiB limit."));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(
        QByteArray(argumentsJson.data(), static_cast<qsizetype>(argumentsJson.size())), &parseError);
    const auto object = document.object();
    const auto commandId = object.value(QStringLiteral("command_id"));
    const QJsonValue timeoutValue = object.value(QStringLiteral("timeout_ms"));
    const auto timeout =
        timeoutValue.isUndefined() ? std::optional<std::uint64_t>{30'000} : unsignedInteger(timeoutValue);
    if (!document.isObject() || parseError.error != QJsonParseError::NoError
        || !hasOnlyKeys(object, {QStringLiteral("command_id"), QStringLiteral("timeout_ms")}) || !commandId.isString()
        || commandId.toString().isEmpty() || !timeout.has_value() || *timeout == 0
        || *timeout > maximumTimeoutMilliseconds)
    {
        return std::unexpected(failure("invalid_arguments", "The wait-command arguments are invalid."));
    }
    return AiWaitCommandRequest{.commandId = commandId.toString().toUtf8().toStdString(),
                                .target = target,
                                .timeoutMilliseconds = static_cast<std::uint32_t>(*timeout)};
}

std::string AiWaitCommandTool::result(const AiTrackedCommand &command)
{
    if (command.state == AiTrackedCommandState::disconnected || command.state == AiTrackedCommandState::outcomeUnknown)
    {
        return json(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("tool_ok"), true},
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), QStringLiteral("outcome_unknown")},
                         {QStringLiteral("message"), QStringLiteral("The command outcome cannot be confirmed.")}}},
            {QStringLiteral("status"), QStringLiteral("outcome_unknown")},
            {QStringLiteral("completion_confirmed"), false},
            {QStringLiteral("command_started"), true},
            {QStringLiteral("command_completed"), false},
            {QStringLiteral("command_succeeded"), QJsonValue::Null},
            {QStringLiteral("command"), commandValue(command, true)}});
    }
    return json(QJsonObject{{QStringLiteral("ok"), true},
                            {QStringLiteral("tool_ok"), true},
                            {QStringLiteral("status"), QStringLiteral("completed")},
                            {QStringLiteral("completion_confirmed"), true},
                            {QStringLiteral("command_started"), true},
                            {QStringLiteral("command_completed"), true},
                            {QStringLiteral("command_succeeded"), commandSucceeded(command)},
                            {QStringLiteral("command"), commandValue(command, true)}});
}

std::string AiWaitCommandTool::timeout(const AiTrackedCommand &command)
{
    return json(QJsonObject{{QStringLiteral("ok"), false},
                            {QStringLiteral("tool_ok"), true},
                            {QStringLiteral("error"),
                             QJsonObject{{QStringLiteral("code"), QStringLiteral("timeout")},
                                         {QStringLiteral("message"), QStringLiteral("The command wait timed out.")}}},
                            {QStringLiteral("status"), QStringLiteral("timeout")},
                            {QStringLiteral("completion_confirmed"), false},
                            {QStringLiteral("command_started"), true},
                            {QStringLiteral("command_completed"), false},
                            {QStringLiteral("command_succeeded"), QJsonValue::Null},
                            {QStringLiteral("command"), commandValue(command, true)}});
}

std::string AiWaitCommandTool::failure(const std::string_view code, const std::string_view message)
{
    return json(QJsonObject{{QStringLiteral("ok"), false},
                            {QStringLiteral("tool_ok"), false},
                            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), text(code)},
                                                                  {QStringLiteral("message"), text(message)}}}});
}

} // namespace ztermy::ai
