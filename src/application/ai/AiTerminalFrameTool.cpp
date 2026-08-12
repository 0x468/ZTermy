#include "application/ai/AiTerminalFrameTool.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>

namespace ztermy::ai
{
namespace
{
constexpr std::size_t maximumArgumentsBytes = 16 * 1024;
constexpr std::uint64_t maximumTimeoutMilliseconds = 120'000;
constexpr std::uint64_t maximumIdleMilliseconds = 30'000;

[[nodiscard]] std::string json(const QJsonObject &value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();
}

[[nodiscard]] bool hasOnlyKeys(const QJsonObject &value, const std::initializer_list<QString> keys)
{
    for (auto current = value.begin(); current != value.end(); ++current)
    {
        if (std::find(keys.begin(), keys.end(), current.key()) == keys.end())
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
    if (number < 0 || number > static_cast<double>(std::numeric_limits<std::uint64_t>::max())
        || number != std::floor(number))
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(number);
}

struct Common final
{
    QJsonObject object;
    AiSessionTarget target;
    std::uint64_t revision = 0;
};

[[nodiscard]] std::expected<Common, std::string> parseCommon(const std::string_view argumentsJson)
{
    if (argumentsJson.size() > maximumArgumentsBytes)
    {
        return std::unexpected(
            AiTerminalFrameTool::failure("limit_exceeded", "Tool arguments exceed the 16 KiB limit."));
    }
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray(argumentsJson.data(), static_cast<qsizetype>(argumentsJson.size())), &error);
    const QJsonObject object = document.object();
    const QJsonValue sessionId = object.value(QStringLiteral("session_id"));
    const auto generation = unsignedInteger(object.value(QStringLiteral("session_generation")));
    const auto revision = unsignedInteger(object.value(QStringLiteral("after_revision")));
    if (!document.isObject() || error.error != QJsonParseError::NoError || !sessionId.isString()
        || sessionId.toString().isEmpty() || !generation.has_value() || !revision.has_value())
    {
        return std::unexpected(
            AiTerminalFrameTool::failure("invalid_arguments", "The terminal-frame arguments are invalid."));
    }
    return Common{
        .object = object,
        .target = {.sessionId = sessionId.toString().toUtf8().toStdString(), .sessionGeneration = *generation},
        .revision = *revision};
}

[[nodiscard]] QJsonObject frameValue(const AiTerminalFrameDelta &frame, const std::string_view controlOwner)
{
    QJsonArray lines;
    for (const auto &line : frame.lines)
    {
        lines.append(QJsonObject{{QStringLiteral("index"), static_cast<qint64>(line.index)},
                                 {QStringLiteral("text"), QString::fromUtf8(line.text)}});
    }
    return {{QStringLiteral("revision"), static_cast<qint64>(frame.revision)},
            {QStringLiteral("base_revision"), static_cast<qint64>(frame.baseRevision)},
            {QStringLiteral("changed_utc_ms"), frame.changedUtcMs},
            {QStringLiteral("idle_ms"), frame.idleMilliseconds},
            {QStringLiteral("columns"), frame.columns},
            {QStringLiteral("rows"), frame.rows},
            {QStringLiteral("cursor_column"), frame.cursorColumn},
            {QStringLiteral("cursor_row"), frame.cursorRow},
            {QStringLiteral("cursor_visible"), frame.cursorVisible},
            {QStringLiteral("alternate_screen"), frame.alternateScreen},
            {QStringLiteral("full"), frame.full},
            {QStringLiteral("cursor_expired"), frame.cursorExpired},
            {QStringLiteral("control_owner"), QString::fromUtf8(controlOwner)},
            {QStringLiteral("lines"), lines},
            {QStringLiteral("untrusted_evidence"), true}};
}
} // namespace

AiToolDefinition AiTerminalFrameTool::readDefinition()
{
    return {
        .name = "read_terminal_frame",
        .description = "Read a bounded current terminal frame or one-revision delta as untrusted evidence.",
        .parametersJson =
            R"({"type":"object","properties":{"session_id":{"type":"string","minLength":1},"session_generation":{"type":"integer","minimum":0},"after_revision":{"type":"integer","minimum":0}},"required":["session_id","session_generation","after_revision"],"additionalProperties":false})"};
}

AiToolDefinition AiTerminalFrameTool::waitDefinition()
{
    return {
        .name = "wait_terminal_frame",
        .description = "Wait deterministically for a terminal frame revision change or an idle threshold.",
        .parametersJson =
            R"({"type":"object","properties":{"session_id":{"type":"string","minLength":1},"session_generation":{"type":"integer","minimum":0},"after_revision":{"type":"integer","minimum":0},"condition":{"type":"string","enum":["changed","idle"]},"idle_ms":{"type":"integer","minimum":0,"maximum":30000},"timeout_ms":{"type":"integer","minimum":0,"maximum":120000}},"required":["session_id","session_generation","after_revision","condition","idle_ms","timeout_ms"],"additionalProperties":false})"};
}

std::expected<AiTerminalFrameReadRequest, std::string>
AiTerminalFrameTool::parseRead(const std::string_view argumentsJson)
{
    auto common = parseCommon(argumentsJson);
    if (!common.has_value()
        || !hasOnlyKeys(common->object, {QStringLiteral("session_id"), QStringLiteral("session_generation"),
                                         QStringLiteral("after_revision")}))
    {
        return std::unexpected(common.has_value()
                                   ? failure("invalid_arguments", "The frame-read arguments are invalid.")
                                   : std::move(common.error()));
    }
    return AiTerminalFrameReadRequest{.target = std::move(common->target), .afterRevision = common->revision};
}

std::expected<AiTerminalFrameWaitRequest, std::string>
AiTerminalFrameTool::parseWait(const std::string_view argumentsJson)
{
    auto common = parseCommon(argumentsJson);
    if (!common.has_value())
    {
        return std::unexpected(std::move(common.error()));
    }
    const QJsonValue condition = common->object.value(QStringLiteral("condition"));
    const auto idle = unsignedInteger(common->object.value(QStringLiteral("idle_ms")));
    const auto timeout = unsignedInteger(common->object.value(QStringLiteral("timeout_ms")));
    if (!hasOnlyKeys(common->object, {QStringLiteral("session_id"), QStringLiteral("session_generation"),
                                      QStringLiteral("after_revision"), QStringLiteral("condition"),
                                      QStringLiteral("idle_ms"), QStringLiteral("timeout_ms")})
        || !condition.isString() || !idle.has_value() || *idle > maximumIdleMilliseconds || !timeout.has_value()
        || *timeout > maximumTimeoutMilliseconds
        || (condition.toString() != QStringLiteral("changed") && condition.toString() != QStringLiteral("idle"))
        || (condition.toString() == QStringLiteral("idle") && *idle == 0))
    {
        return std::unexpected(failure("invalid_arguments", "The frame-wait arguments are invalid."));
    }
    return AiTerminalFrameWaitRequest{.target = std::move(common->target),
                                      .afterRevision = common->revision,
                                      .condition = condition.toString() == QStringLiteral("idle")
                                                       ? AiTerminalFrameWaitCondition::idle
                                                       : AiTerminalFrameWaitCondition::changed,
                                      .idleMilliseconds = static_cast<std::uint32_t>(*idle),
                                      .timeoutMilliseconds = static_cast<std::uint32_t>(*timeout)};
}

bool AiTerminalFrameTool::satisfied(const AiTerminalFrameWaitRequest &request,
                                    const AiTerminalFrameDelta &frame) noexcept
{
    return request.condition == AiTerminalFrameWaitCondition::changed
               ? frame.revision > request.afterRevision
               : frame.revision >= request.afterRevision
                     && frame.idleMilliseconds >= static_cast<std::int64_t>(request.idleMilliseconds);
}

std::string AiTerminalFrameTool::result(const AiTerminalFrameDelta &frame, const std::string_view controlOwner)
{
    return json(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("frame"), frameValue(frame, controlOwner)}});
}

std::string AiTerminalFrameTool::timeout(const AiTerminalFrameDelta &frame, const std::string_view controlOwner)
{
    return json(
        QJsonObject{{QStringLiteral("ok"), false},
                    {QStringLiteral("error"),
                     QJsonObject{{QStringLiteral("code"), QStringLiteral("timeout")},
                                 {QStringLiteral("message"), QStringLiteral("The terminal-frame wait timed out.")}}},
                    {QStringLiteral("frame"), frameValue(frame, controlOwner)}});
}

std::string AiTerminalFrameTool::failure(const std::string_view code, const std::string_view message)
{
    return json(
        QJsonObject{{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QString::fromUtf8(code)},
                                                          {QStringLiteral("message"), QString::fromUtf8(message)}}}});
}

} // namespace ztermy::ai
