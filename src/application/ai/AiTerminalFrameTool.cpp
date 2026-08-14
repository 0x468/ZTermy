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
#include <utility>

namespace ztermy::ai
{
namespace
{
constexpr std::size_t maximumArgumentsBytes = std::size_t{16} * 1024;
constexpr std::uint64_t maximumTimeoutMilliseconds = 120'000;
constexpr std::uint64_t maximumIdleMilliseconds = 30'000;
constexpr std::uint64_t defaultTimeoutMilliseconds = 30'000;
constexpr std::uint64_t defaultIdleMilliseconds = 750;

[[nodiscard]] std::string json(const QJsonObject &value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();
}

[[nodiscard]] bool hasOnlyKeys(const QJsonObject &value, const std::initializer_list<QString> keys)
{
    for (auto current = value.begin(); current != value.end(); ++current)
    {
        if (std::ranges::find(keys, current.key()) == keys.end())
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

[[nodiscard]] std::expected<Common, std::string> parseCommon(const std::string_view argumentsJson,
                                                             const AiSessionTarget &target)
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
    const auto revision = unsignedInteger(object.value(QStringLiteral("after_revision")));
    if (!document.isObject() || error.error != QJsonParseError::NoError || !revision.has_value())
    {
        return std::unexpected(
            AiTerminalFrameTool::failure("invalid_arguments", "The terminal-frame arguments are invalid."));
    }
    return Common{
        .object = object,
        .target = target,
        .revision = *revision};
}

[[nodiscard]] QJsonObject frameValue(const AiTerminalFrameDelta &frame, const std::string_view controlOwner,
                                     const AiTerminalInteractionCapability &capability)
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
            {QStringLiteral("dropped_output_observations"), static_cast<qint64>(frame.droppedOutputObservations)},
            {QStringLiteral("full"), frame.full},
            {QStringLiteral("cursor_expired"), frame.cursorExpired},
            {QStringLiteral("control_owner"), QString::fromUtf8(controlOwner)},
            {QStringLiteral("capability"),
             QJsonObject{{QStringLiteral("shell_family"), QString::fromUtf8(capability.shellFamily)},
                         {QStringLiteral("semantic_quality"), QString::fromUtf8(capability.semanticQuality)},
                         {QStringLiteral("observation_mode"), QString::fromUtf8(capability.observationMode)},
                         {QStringLiteral("degraded_reason"), QString::fromUtf8(capability.degradedReason)},
                         {QStringLiteral("exact_command_boundaries"), capability.exactCommandBoundaries},
                         {QStringLiteral("reliable_exit_status"), capability.reliableExitStatus},
                         {QStringLiteral("frame_deltas"), capability.frameDeltas}}},
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
            R"({"type":"object","properties":{"after_revision":{"type":"integer","minimum":0}},"required":["after_revision"],"additionalProperties":false})"};
}

AiToolDefinition AiTerminalFrameTool::waitDefinition()
{
    return {
        .name = "wait_terminal_frame",
        .description =
            "Wait for a terminal frame change (the default), or for frame idleness. Set condition to idle only "
            "when an idle threshold is needed; idle_ms then defaults to 750. timeout_ms defaults to 30000.",
        .parametersJson =
            R"({"type":"object","properties":{"after_revision":{"type":"integer","minimum":0},"condition":{"type":"string","enum":["changed","idle"],"default":"changed"},"idle_ms":{"type":"integer","minimum":0,"maximum":30000,"default":750},"timeout_ms":{"type":"integer","minimum":1,"maximum":120000,"default":30000}},"required":["after_revision"],"additionalProperties":false})"};
}

std::expected<AiTerminalFrameReadRequest, std::string>
AiTerminalFrameTool::parseRead(const std::string_view argumentsJson, const AiSessionTarget &target)
{
    auto common = parseCommon(argumentsJson, target);
    if (!common.has_value() || !hasOnlyKeys(common->object, {QStringLiteral("after_revision")}))
    {
        return std::unexpected(common.has_value()
                                   ? failure("invalid_arguments", "The frame-read arguments are invalid.")
                                   : std::move(common.error()));
    }
    return AiTerminalFrameReadRequest{.target = std::move(common->target), .afterRevision = common->revision};
}

std::expected<AiTerminalFrameWaitRequest, std::string>
AiTerminalFrameTool::parseWait(const std::string_view argumentsJson, const AiSessionTarget &target)
{
    auto common = parseCommon(argumentsJson, target);
    if (!common.has_value())
    {
        return std::unexpected(std::move(common.error()));
    }
    const QJsonValue conditionValue = common->object.value(QStringLiteral("condition"));
    const QString condition = conditionValue.isUndefined() ? QStringLiteral("changed") : conditionValue.toString();
    const QJsonValue idleValue = common->object.value(QStringLiteral("idle_ms"));
    const QJsonValue timeoutValue = common->object.value(QStringLiteral("timeout_ms"));
    const auto idle =
        idleValue.isUndefined() ? std::optional<std::uint64_t>{defaultIdleMilliseconds} : unsignedInteger(idleValue);
    const auto timeout = timeoutValue.isUndefined() ? std::optional<std::uint64_t>{defaultTimeoutMilliseconds}
                                                    : unsignedInteger(timeoutValue);
    if (!hasOnlyKeys(common->object, {QStringLiteral("after_revision"), QStringLiteral("condition"),
                                      QStringLiteral("idle_ms"), QStringLiteral("timeout_ms")})
        || (!conditionValue.isUndefined() && !conditionValue.isString()) || !idle.has_value()
        || *idle > maximumIdleMilliseconds || (condition == QStringLiteral("idle") && *idle == 0)
        || !timeout.has_value() || *timeout == 0 || *timeout > maximumTimeoutMilliseconds
        || (condition != QStringLiteral("changed") && condition != QStringLiteral("idle")))
    {
        return std::unexpected(failure("invalid_arguments", "The frame-wait arguments are invalid."));
    }
    return AiTerminalFrameWaitRequest{
        .target = std::move(common->target),
        .afterRevision = common->revision,
        .condition = condition == QStringLiteral("idle") ? AiTerminalFrameWaitCondition::idle
                                                         : AiTerminalFrameWaitCondition::changed,
        .idleMilliseconds = condition == QStringLiteral("idle") ? static_cast<std::uint32_t>(*idle) : 0,
        .timeoutMilliseconds = static_cast<std::uint32_t>(*timeout)};
}

bool AiTerminalFrameTool::satisfied(const AiTerminalFrameWaitRequest &request,
                                    const AiTerminalFrameDelta &frame) noexcept
{
    return request.condition == AiTerminalFrameWaitCondition::changed
               ? frame.revision > request.afterRevision
               : frame.revision >= request.afterRevision
                     && std::cmp_greater_equal(frame.idleMilliseconds, request.idleMilliseconds);
}

std::string AiTerminalFrameTool::result(const AiTerminalFrameDelta &frame, const std::string_view controlOwner,
                                        const AiTerminalInteractionCapability &capability)
{
    return json(QJsonObject{{QStringLiteral("ok"), true},
                            {QStringLiteral("frame"), frameValue(frame, controlOwner, capability)}});
}

std::string AiTerminalFrameTool::timeout(const AiTerminalFrameDelta &frame, const std::string_view controlOwner,
                                         const AiTerminalInteractionCapability &capability)
{
    return json(
        QJsonObject{{QStringLiteral("ok"), false},
                    {QStringLiteral("error"),
                     QJsonObject{{QStringLiteral("code"), QStringLiteral("timeout")},
                                 {QStringLiteral("message"), QStringLiteral("The terminal-frame wait timed out.")}}},
                    {QStringLiteral("frame"), frameValue(frame, controlOwner, capability)}});
}

std::string AiTerminalFrameTool::failure(const std::string_view code, const std::string_view message)
{
    return json(
        QJsonObject{{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QString::fromUtf8(code)},
                                                          {QStringLiteral("message"), QString::fromUtf8(message)}}}});
}

} // namespace ztermy::ai
