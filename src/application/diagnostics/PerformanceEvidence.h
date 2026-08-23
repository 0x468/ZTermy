#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <cstdint>
#include <expected>

namespace ztermy::diagnostics
{

struct PerformanceLatencyEvidence final
{
    std::uint64_t samples = 0;
    std::uint64_t p50Microseconds = 0;
    std::uint64_t p95Microseconds = 0;
    std::uint64_t p99Microseconds = 0;
    std::uint64_t maximumMicroseconds = 0;
};

struct PerformanceEvidence final
{
    int schemaVersion = 0;
    QString applicationVersion;
    QString qtVersion;
    QString buildType;
    QString graphicsApi;
    QString backdrop;
    qreal devicePixelRatio = 0.0;
    qreal terminalBackgroundOpacity = 0.0;
    int logicalWidth = 0;
    int logicalHeight = 0;
    int alphaBufferBits = -1;
    bool preferSoftwareRenderer = false;

    bool completed = false;
    bool responsive = false;
    bool progressiveFrames = false;
    bool terminalRendered = false;
    bool scrollbarPassed = false;
    bool resizeCompleted = false;
    std::uint64_t completionMilliseconds = 0;
    std::uint64_t heartbeatTicks = 0;
    std::uint64_t maximumHeartbeatGapMilliseconds = 0;
    std::uint64_t frameSwaps = 0;
    std::uint64_t idleDurationMilliseconds = 0;
    std::uint64_t idleFrameSwaps = 0;

    PerformanceLatencyEvidence paint;
    PerformanceLatencyEvidence textureCreate;
    std::uint64_t renderedFrames = 0;
    std::uint64_t uploadedBytes = 0;
    std::uint64_t cursorInvalidations = 0;
    std::uint64_t snapshotUpdates = 0;

    PerformanceLatencyEvidence idlePaint;
    PerformanceLatencyEvidence idleTextureCreate;
    std::uint64_t idleRenderedFrames = 0;
    std::uint64_t idleUploadedBytes = 0;
    std::uint64_t idleCursorInvalidations = 0;

    [[nodiscard]] static std::expected<PerformanceEvidence, QString> parse(const QByteArray &json);
    [[nodiscard]] QStringList validationIssues() const;
    [[nodiscard]] QStringList comparabilityIssues(const PerformanceEvidence &candidate) const;
    [[nodiscard]] QString comparisonMarkdown(const PerformanceEvidence &candidate) const;
};

} // namespace ztermy::diagnostics
