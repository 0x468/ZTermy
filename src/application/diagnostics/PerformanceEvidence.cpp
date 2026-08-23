#include "application/diagnostics/PerformanceEvidence.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QtGlobal>

#include <cmath>
#include <limits>

namespace ztermy::diagnostics
{
namespace
{

[[nodiscard]] std::expected<QJsonObject, QString> requiredObject(const QJsonObject &parent, const QString &key)
{
    const QJsonValue value = parent.value(key);
    if (!value.isObject())
    {
        return std::unexpected(QStringLiteral("Missing object: %1").arg(key));
    }
    return value.toObject();
}

[[nodiscard]] std::expected<std::uint64_t, QString> unsignedInteger(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble())
    {
        return std::unexpected(QStringLiteral("Missing number: %1").arg(key));
    }
    const double number = value.toDouble(-1.0);
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
    {
        return std::unexpected(QStringLiteral("Invalid unsigned integer: %1").arg(key));
    }
    return static_cast<std::uint64_t>(number);
}

[[nodiscard]] std::expected<int, QString> integer(const QJsonObject &object, const QString &key)
{
    const auto value = unsignedInteger(object, key);
    if (!value || *value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected(value ? QStringLiteral("Integer is out of range: %1").arg(key) : value.error());
    }
    return static_cast<int>(*value);
}

[[nodiscard]] std::expected<QString, QString> string(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().isEmpty())
    {
        return std::unexpected(QStringLiteral("Missing string: %1").arg(key));
    }
    return value.toString();
}

[[nodiscard]] std::expected<bool, QString> boolean(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (!value.isBool())
    {
        return std::unexpected(QStringLiteral("Missing boolean: %1").arg(key));
    }
    return value.toBool();
}

[[nodiscard]] std::expected<qreal, QString> real(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
    {
        return std::unexpected(QStringLiteral("Missing finite number: %1").arg(key));
    }
    return static_cast<qreal>(value.toDouble());
}

[[nodiscard]] std::expected<PerformanceLatencyEvidence, QString> latency(const QJsonObject &object)
{
    const auto samples = unsignedInteger(object, QStringLiteral("samples"));
    const auto p50 = unsignedInteger(object, QStringLiteral("p50UpperBoundUs"));
    const auto p95 = unsignedInteger(object, QStringLiteral("p95UpperBoundUs"));
    const auto p99 = unsignedInteger(object, QStringLiteral("p99UpperBoundUs"));
    const auto maximum = unsignedInteger(object, QStringLiteral("maxUs"));
    if (!samples || !p50 || !p95 || !p99 || !maximum)
    {
        return std::unexpected(!samples ? samples.error()
                               : !p50   ? p50.error()
                               : !p95   ? p95.error()
                               : !p99   ? p99.error()
                                        : maximum.error());
    }
    return PerformanceLatencyEvidence{.samples = *samples,
                                      .p50Microseconds = *p50,
                                      .p95Microseconds = *p95,
                                      .p99Microseconds = *p99,
                                      .maximumMicroseconds = *maximum};
}

[[nodiscard]] QString percentageChange(const std::uint64_t baseline, const std::uint64_t candidate)
{
    if (baseline == 0)
    {
        return candidate == 0 ? QStringLiteral("0.0%") : QStringLiteral("n/a");
    }
    const double change =
        ((static_cast<double>(candidate) - static_cast<double>(baseline)) / static_cast<double>(baseline)) * 100.0;
    return QStringLiteral("%1%2%").arg(change > 0.0 ? QStringLiteral("+") : QString{}, QString::number(change, 'f', 1));
}

void appendComparisonRow(QString &markdown, const QString &name, const std::uint64_t baseline,
                         const std::uint64_t candidate, const QString &unit)
{
    markdown += QStringLiteral("| %1 | %2 %3 | %4 %3 | %5 |\n")
                    .arg(name, QString::number(baseline), unit, QString::number(candidate),
                         percentageChange(baseline, candidate));
}

} // namespace

std::expected<PerformanceEvidence, QString> PerformanceEvidence::parse(const QByteArray &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(QStringLiteral("Invalid performance JSON: %1").arg(parseError.errorString()));
    }

    const QJsonObject root = document.object();
    const auto environment = requiredObject(root, QStringLiteral("environment"));
    const auto scenario = requiredObject(root, QStringLiteral("scenario"));
    const auto renderer = requiredObject(root, QStringLiteral("terminalRenderer"));
    if (!environment || !scenario || !renderer)
    {
        return std::unexpected(!environment ? environment.error() : !scenario ? scenario.error() : renderer.error());
    }
    const auto paintObject = requiredObject(*renderer, QStringLiteral("paint"));
    const auto textureObject = requiredObject(*renderer, QStringLiteral("textureCreate"));
    if (!paintObject || !textureObject)
    {
        return std::unexpected(!paintObject ? paintObject.error() : textureObject.error());
    }

    const auto schemaVersionValue = integer(root, QStringLiteral("schemaVersion"));
    const auto applicationVersionValue = string(*environment, QStringLiteral("applicationVersion"));
    const auto qtVersionValue = string(*environment, QStringLiteral("qtVersion"));
    const auto buildTypeValue = string(*environment, QStringLiteral("buildType"));
    const auto graphicsApiValue = string(*environment, QStringLiteral("graphicsApi"));
    const auto backdropValue = string(*environment, QStringLiteral("backdrop"));
    const auto devicePixelRatioValue = real(*environment, QStringLiteral("devicePixelRatio"));
    const auto terminalOpacityValue = real(*environment, QStringLiteral("terminalBackgroundOpacity"));
    const auto logicalWidthValue = integer(*environment, QStringLiteral("logicalWidth"));
    const auto logicalHeightValue = integer(*environment, QStringLiteral("logicalHeight"));
    const auto alphaBufferBitsValue = integer(*environment, QStringLiteral("alphaBufferBits"));
    const auto preferSoftwareValue = boolean(*environment, QStringLiteral("preferSoftwareRenderer"));
    const auto completedValue = boolean(*scenario, QStringLiteral("completed"));
    const auto responsiveValue = boolean(*scenario, QStringLiteral("responsive"));
    const auto progressiveValue = boolean(*scenario, QStringLiteral("progressiveFrames"));
    const auto terminalRenderedValue = boolean(*scenario, QStringLiteral("terminalRendered"));
    const auto scrollbarPassedValue = boolean(*scenario, QStringLiteral("scrollbarPassed"));
    const auto resizeCompletedValue = boolean(*scenario, QStringLiteral("resizeCompleted"));
    const auto completionValue = unsignedInteger(*scenario, QStringLiteral("completionMs"));
    const auto heartbeatTicksValue = unsignedInteger(*scenario, QStringLiteral("heartbeatTicks"));
    const auto heartbeatGapValue = unsignedInteger(*scenario, QStringLiteral("maximumHeartbeatGapMs"));
    const auto frameSwapsValue = unsignedInteger(*scenario, QStringLiteral("frameSwaps"));
    const auto paintValue = latency(*paintObject);
    const auto textureValue = latency(*textureObject);
    const auto renderedFramesValue = unsignedInteger(*renderer, QStringLiteral("renderedFrames"));
    const auto uploadedBytesValue = unsignedInteger(*renderer, QStringLiteral("uploadedBytes"));
    const auto cursorInvalidationsValue = unsignedInteger(*renderer, QStringLiteral("cursorInvalidations"));
    const auto snapshotUpdatesValue = unsignedInteger(*renderer, QStringLiteral("snapshotUpdates"));

    QString firstError;
    const auto captureFirstError = [&firstError](const auto &value) {
        if (firstError.isEmpty() && !value)
        {
            firstError = value.error();
        }
    };
    captureFirstError(schemaVersionValue);
    captureFirstError(applicationVersionValue);
    captureFirstError(qtVersionValue);
    captureFirstError(buildTypeValue);
    captureFirstError(graphicsApiValue);
    captureFirstError(backdropValue);
    captureFirstError(devicePixelRatioValue);
    captureFirstError(terminalOpacityValue);
    captureFirstError(logicalWidthValue);
    captureFirstError(logicalHeightValue);
    captureFirstError(alphaBufferBitsValue);
    captureFirstError(preferSoftwareValue);
    captureFirstError(completedValue);
    captureFirstError(responsiveValue);
    captureFirstError(progressiveValue);
    captureFirstError(terminalRenderedValue);
    captureFirstError(scrollbarPassedValue);
    captureFirstError(resizeCompletedValue);
    captureFirstError(completionValue);
    captureFirstError(heartbeatTicksValue);
    captureFirstError(heartbeatGapValue);
    captureFirstError(frameSwapsValue);
    captureFirstError(paintValue);
    captureFirstError(textureValue);
    captureFirstError(renderedFramesValue);
    captureFirstError(uploadedBytesValue);
    captureFirstError(cursorInvalidationsValue);
    captureFirstError(snapshotUpdatesValue);
    if (!firstError.isEmpty())
    {
        return std::unexpected(firstError);
    }

    return PerformanceEvidence{
        .schemaVersion = *schemaVersionValue,
        .applicationVersion = *applicationVersionValue,
        .qtVersion = *qtVersionValue,
        .buildType = *buildTypeValue,
        .graphicsApi = *graphicsApiValue,
        .backdrop = *backdropValue,
        .devicePixelRatio = *devicePixelRatioValue,
        .terminalBackgroundOpacity = *terminalOpacityValue,
        .logicalWidth = *logicalWidthValue,
        .logicalHeight = *logicalHeightValue,
        .alphaBufferBits = *alphaBufferBitsValue,
        .preferSoftwareRenderer = *preferSoftwareValue,
        .completed = *completedValue,
        .responsive = *responsiveValue,
        .progressiveFrames = *progressiveValue,
        .terminalRendered = *terminalRenderedValue,
        .scrollbarPassed = *scrollbarPassedValue,
        .resizeCompleted = *resizeCompletedValue,
        .completionMilliseconds = *completionValue,
        .heartbeatTicks = *heartbeatTicksValue,
        .maximumHeartbeatGapMilliseconds = *heartbeatGapValue,
        .frameSwaps = *frameSwapsValue,
        .paint = *paintValue,
        .textureCreate = *textureValue,
        .renderedFrames = *renderedFramesValue,
        .uploadedBytes = *uploadedBytesValue,
        .cursorInvalidations = *cursorInvalidationsValue,
        .snapshotUpdates = *snapshotUpdatesValue,
    };
}

QStringList PerformanceEvidence::validationIssues() const
{
    QStringList issues;
    if (schemaVersion != 1)
    {
        issues.push_back(QStringLiteral("Unsupported schema version."));
    }
    if (buildType != QStringLiteral("release"))
    {
        issues.push_back(QStringLiteral("Performance decisions require a Release build."));
    }
    if (graphicsApi == QStringLiteral("unknown") || graphicsApi == QStringLiteral("null"))
    {
        issues.push_back(QStringLiteral("No usable graphics backend was recorded."));
    }
    if (graphicsApi == QStringLiteral("software") && !preferSoftwareRenderer)
    {
        issues.push_back(QStringLiteral("The scene graph unexpectedly selected software rendering."));
    }
    if (devicePixelRatio <= 0.0 || logicalWidth <= 0 || logicalHeight <= 0)
    {
        issues.push_back(QStringLiteral("The window geometry or device-pixel ratio is invalid."));
    }
    if (!completed || !responsive || !progressiveFrames || !terminalRendered || !scrollbarPassed || !resizeCompleted)
    {
        issues.push_back(QStringLiteral("The benchmark scenario did not complete all functional checks."));
    }
    if (frameSwaps < 30)
    {
        issues.push_back(QStringLiteral("Fewer than 30 frame swaps were observed."));
    }
    if (paint.samples < 30 || textureCreate.samples < 30 || renderedFrames < 30)
    {
        issues.push_back(QStringLiteral("The terminal renderer has fewer than 30 timing samples."));
    }
    return issues;
}

QStringList PerformanceEvidence::comparabilityIssues(const PerformanceEvidence &candidate) const
{
    QStringList issues;
    const auto requireEqual = [&issues](const bool equal, const QString &name) {
        if (!equal)
        {
            issues.push_back(QStringLiteral("Different %1.").arg(name));
        }
    };
    requireEqual(buildType == candidate.buildType, QStringLiteral("build type"));
    requireEqual(qtVersion == candidate.qtVersion, QStringLiteral("Qt version"));
    requireEqual(graphicsApi == candidate.graphicsApi, QStringLiteral("graphics backend"));
    requireEqual(qFuzzyCompare(devicePixelRatio, candidate.devicePixelRatio), QStringLiteral("device-pixel ratio"));
    requireEqual(logicalWidth == candidate.logicalWidth && logicalHeight == candidate.logicalHeight,
                 QStringLiteral("window size"));
    requireEqual(backdrop == candidate.backdrop, QStringLiteral("backdrop"));
    requireEqual(qFuzzyCompare(terminalBackgroundOpacity, candidate.terminalBackgroundOpacity),
                 QStringLiteral("terminal background opacity"));
    requireEqual(preferSoftwareRenderer == candidate.preferSoftwareRenderer,
                 QStringLiteral("software-renderer preference"));
    return issues;
}

QString PerformanceEvidence::comparisonMarkdown(const PerformanceEvidence &candidate) const
{
    QString markdown = QStringLiteral("# ztermy performance comparison\n\n"
                                      "| Metric | Baseline | Candidate | Change |\n"
                                      "|---|---:|---:|---:|\n");
    appendComparisonRow(markdown, QStringLiteral("Paint P95"), paint.p95Microseconds, candidate.paint.p95Microseconds,
                        QStringLiteral("us"));
    appendComparisonRow(markdown, QStringLiteral("Texture creation P95"), textureCreate.p95Microseconds,
                        candidate.textureCreate.p95Microseconds, QStringLiteral("us"));
    appendComparisonRow(markdown, QStringLiteral("Paint maximum"), paint.maximumMicroseconds,
                        candidate.paint.maximumMicroseconds, QStringLiteral("us"));
    appendComparisonRow(markdown, QStringLiteral("Maximum heartbeat gap"), maximumHeartbeatGapMilliseconds,
                        candidate.maximumHeartbeatGapMilliseconds, QStringLiteral("ms"));
    appendComparisonRow(markdown, QStringLiteral("Scenario completion"), completionMilliseconds,
                        candidate.completionMilliseconds, QStringLiteral("ms"));
    appendComparisonRow(markdown, QStringLiteral("Estimated texture upload"), uploadedBytes, candidate.uploadedBytes,
                        QStringLiteral("bytes"));
    appendComparisonRow(markdown, QStringLiteral("Rendered frames"), renderedFrames, candidate.renderedFrames,
                        QStringLiteral("frames"));
    appendComparisonRow(markdown, QStringLiteral("Snapshot updates"), snapshotUpdates, candidate.snapshotUpdates,
                        QStringLiteral("updates"));
    appendComparisonRow(markdown, QStringLiteral("Cursor invalidations"), cursorInvalidations,
                        candidate.cursorInvalidations, QStringLiteral("updates"));
    return markdown;
}

} // namespace ztermy::diagnostics
