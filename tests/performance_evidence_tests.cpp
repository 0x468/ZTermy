#include "application/diagnostics/PerformanceEvidence.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

namespace
{

[[nodiscard]] QJsonObject latency(const int p95)
{
    return {{QStringLiteral("samples"), 60},
            {QStringLiteral("p50UpperBoundUs"), p95 / 2},
            {QStringLiteral("p95UpperBoundUs"), p95},
            {QStringLiteral("p99UpperBoundUs"), p95 * 2},
            {QStringLiteral("maxUs"), p95 * 3}};
}

[[nodiscard]] QByteArray report(const QString &buildType = QStringLiteral("release"), const int paintP95 = 8000,
                                const qreal dpr = 1.0)
{
    return QJsonDocument(
               QJsonObject{
                   {QStringLiteral("schemaVersion"), 2},
                   {QStringLiteral("environment"),
                    QJsonObject{{QStringLiteral("applicationVersion"), QStringLiteral("0.3.0")},
                                {QStringLiteral("qtVersion"), QStringLiteral("6.8.3")},
                                {QStringLiteral("buildType"), buildType},
                                {QStringLiteral("graphicsApi"), QStringLiteral("direct3d11")},
                                {QStringLiteral("backdrop"), QStringLiteral("acrylic")},
                                {QStringLiteral("devicePixelRatio"), dpr},
                                {QStringLiteral("terminalBackgroundOpacity"), 1.0},
                                {QStringLiteral("logicalWidth"), 1120},
                                {QStringLiteral("logicalHeight"), 800},
                                {QStringLiteral("alphaBufferBits"), 8},
                                {QStringLiteral("preferSoftwareRenderer"), false}}},
                   {QStringLiteral("scenario"), QJsonObject{{QStringLiteral("completed"), true},
                                                            {QStringLiteral("responsive"), true},
                                                            {QStringLiteral("progressiveFrames"), true},
                                                            {QStringLiteral("terminalRendered"), true},
                                                            {QStringLiteral("scrollbarPassed"), true},
                                                            {QStringLiteral("resizeCompleted"), true},
                                                            {QStringLiteral("completionMs"), 2000},
                                                            {QStringLiteral("heartbeatTicks"), 200},
                                                            {QStringLiteral("maximumHeartbeatGapMs"), 20},
                                                            {QStringLiteral("frameSwaps"), 60},
                                                            {QStringLiteral("idleDurationMs"), 2200},
                                                            {QStringLiteral("idleFrameSwaps"), 4}}},
                   {QStringLiteral("terminalRenderer"), QJsonObject{{QStringLiteral("paint"), latency(paintP95)},
                                                                    {QStringLiteral("textureCreate"), latency(2000)},
                                                                    {QStringLiteral("renderedFrames"), 60},
                                                                    {QStringLiteral("uploadedBytes"), 120'000'000},
                                                                    {QStringLiteral("cursorInvalidations"), 4},
                                                                    {QStringLiteral("snapshotUpdates"), 240}}},
                   {QStringLiteral("idleTerminalRenderer"), QJsonObject{{QStringLiteral("paint"), latency(1000)},
                                                                        {QStringLiteral("textureCreate"), latency(500)},
                                                                        {QStringLiteral("renderedFrames"), 4},
                                                                        {QStringLiteral("uploadedBytes"), 12'000'000},
                                                                        {QStringLiteral("cursorInvalidations"), 4}}},
               })
        .toJson(QJsonDocument::Compact);
}

class PerformanceEvidenceTests final : public QObject
{
    Q_OBJECT

private slots:
    void acceptsCompleteReleaseEvidence();
    void rejectsDebugAndUndersampledEvidence();
    void rejectsEnvironmentMismatch();
    void rendersDeterministicComparison();
};

void PerformanceEvidenceTests::acceptsCompleteReleaseEvidence()
{
    const auto evidence = ztermy::diagnostics::PerformanceEvidence::parse(report());
    QVERIFY(evidence.has_value());
    QVERIFY(evidence->validationIssues().isEmpty());
    QCOMPARE(evidence->paint.p95Microseconds, std::uint64_t{8000});
}

void PerformanceEvidenceTests::rejectsDebugAndUndersampledEvidence()
{
    QJsonObject root = QJsonDocument::fromJson(report(QStringLiteral("debug"))).object();
    QJsonObject renderer = root.value(QStringLiteral("terminalRenderer")).toObject();
    QJsonObject paint = renderer.value(QStringLiteral("paint")).toObject();
    paint.insert(QStringLiteral("samples"), 1);
    renderer.insert(QStringLiteral("paint"), paint);
    renderer.insert(QStringLiteral("renderedFrames"), 1);
    root.insert(QStringLiteral("terminalRenderer"), renderer);
    const auto evidence = ztermy::diagnostics::PerformanceEvidence::parse(QJsonDocument(root).toJson());
    QVERIFY(evidence.has_value());
    QCOMPARE(evidence->validationIssues().size(), 2);
}

void PerformanceEvidenceTests::rejectsEnvironmentMismatch()
{
    const auto baseline = ztermy::diagnostics::PerformanceEvidence::parse(report());
    const auto candidate =
        ztermy::diagnostics::PerformanceEvidence::parse(report(QStringLiteral("release"), 6000, 1.5));
    QVERIFY(baseline.has_value());
    QVERIFY(candidate.has_value());
    QCOMPARE(baseline->comparabilityIssues(*candidate), QStringList{QStringLiteral("Different device-pixel ratio.")});
}

void PerformanceEvidenceTests::rendersDeterministicComparison()
{
    const auto baseline = ztermy::diagnostics::PerformanceEvidence::parse(report());
    const auto candidate = ztermy::diagnostics::PerformanceEvidence::parse(report(QStringLiteral("release"), 4000));
    QVERIFY(baseline.has_value());
    QVERIFY(candidate.has_value());
    const QString markdown = baseline->comparisonMarkdown(*candidate);
    QVERIFY(markdown.contains(QStringLiteral("| Paint P95 | 8000 us | 4000 us | -50.0% |")));
    QVERIFY(markdown.contains(QStringLiteral("| Estimated texture upload |")));
}

} // namespace

QTEST_GUILESS_MAIN(PerformanceEvidenceTests)

#include "performance_evidence_tests.moc"
