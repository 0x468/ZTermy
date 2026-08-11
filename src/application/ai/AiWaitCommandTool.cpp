#include "application/ai/AiWaitCommandTool.h"

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
constexpr std::uint32_t maximumTimeoutMilliseconds = 120'000;

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

[[nodiscard]] QJsonObject commandValue(const AiTrackedCommand &command)
{
    QJsonObject value{{QStringLiteral("command_id"), text(command.id)},
                      {QStringLiteral("session_id"), text(command.target.sessionId)},
                      {QStringLiteral("session_generation"), static_cast<qint64>(command.target.sessionGeneration)},
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
    return value;
}

} // namespace

AiToolDefinition AiWaitCommandTool::definition()
{
    return {
        .name = "wait_command",
        .description = "Wait for a tracked command lifecycle change without cancelling the command when the wait "
                       "is cancelled.",
        .parametersJson =
            R"({"type":"object","properties":{"command_id":{"type":"string","minLength":1},"session_id":{"type":"string","minLength":1},"session_generation":{"type":"integer","minimum":0},"timeout_ms":{"type":"integer","minimum":0,"maximum":120000}},"required":["command_id","session_id","session_generation","timeout_ms"],"additionalProperties":false})"};
}

std::expected<AiWaitCommandRequest, std::string> AiWaitCommandTool::parse(const std::string_view argumentsJson)
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
    const auto sessionId = object.value(QStringLiteral("session_id"));
    const auto generation = unsignedInteger(object.value(QStringLiteral("session_generation")));
    const auto timeout = unsignedInteger(object.value(QStringLiteral("timeout_ms")));
    if (!document.isObject() || parseError.error != QJsonParseError::NoError
        || !hasOnlyKeys(object, {QStringLiteral("command_id"), QStringLiteral("session_id"),
                                 QStringLiteral("session_generation"), QStringLiteral("timeout_ms")})
        || !commandId.isString() || commandId.toString().isEmpty() || !sessionId.isString()
        || sessionId.toString().isEmpty() || !generation.has_value() || !timeout.has_value()
        || *timeout > maximumTimeoutMilliseconds)
    {
        return std::unexpected(failure("invalid_arguments", "The wait-command arguments are invalid."));
    }
    return AiWaitCommandRequest{
        .commandId = commandId.toString().toUtf8().toStdString(),
        .target = {.sessionId = sessionId.toString().toUtf8().toStdString(), .sessionGeneration = *generation},
        .timeoutMilliseconds = static_cast<std::uint32_t>(*timeout)};
}

std::string AiWaitCommandTool::result(const AiTrackedCommand &command)
{
    if (command.state == AiTrackedCommandState::disconnected || command.state == AiTrackedCommandState::outcomeUnknown)
    {
        return json(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), QStringLiteral("outcome_unknown")},
                         {QStringLiteral("message"), QStringLiteral("The command outcome cannot be confirmed.")}}},
            {QStringLiteral("command"), commandValue(command)}});
    }
    return json(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("command"), commandValue(command)}});
}

std::string AiWaitCommandTool::timeout(const AiTrackedCommand &command)
{
    return json(QJsonObject{{QStringLiteral("ok"), false},
                            {QStringLiteral("error"),
                             QJsonObject{{QStringLiteral("code"), QStringLiteral("timeout")},
                                         {QStringLiteral("message"), QStringLiteral("The command wait timed out.")}}},
                            {QStringLiteral("command"), commandValue(command)}});
}

std::string AiWaitCommandTool::failure(const std::string_view code, const std::string_view message)
{
    return json(QJsonObject{{QStringLiteral("ok"), false},
                            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), text(code)},
                                                                  {QStringLiteral("message"), text(message)}}}});
}

} // namespace ztermy::ai
