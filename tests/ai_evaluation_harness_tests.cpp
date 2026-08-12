#include "infrastructure/ai/AiEvaluationHarness.h"

#include <QtTest/QTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

namespace
{
class AiEvaluationHarnessTests final : public QObject
{
    Q_OBJECT

private slots:
    void replaysVersionedSyntheticCorpus();
    void reportsMissingEvidenceWithoutRunningProviders();
};

void AiEvaluationHarnessTests::replaysVersionedSyntheticCorpus()
{
    const QString corpus = QFINDTESTDATA("fixtures/ai-eval/v1/corpus.json");
    const QString trace = QFINDTESTDATA("fixtures/ai-eval/v1/synthetic-pass.json");
    QVERIFY(!corpus.isEmpty());
    QVERIFY(!trace.isEmpty());
    const auto result = ztermy::ai::AiEvaluationHarness::replay(corpus, trace);
    QVERIFY(result.has_value());
    QVERIFY(result->value(QStringLiteral("all_passed")).toBool());
    QCOMPARE(result->value(QStringLiteral("passed")).toInt(), 12);
}

void AiEvaluationHarnessTests::reportsMissingEvidenceWithoutRunningProviders()
{
    const QString corpus = QFINDTESTDATA("fixtures/ai-eval/v1/corpus.json");
    const QString sourceTrace = QFINDTESTDATA("fixtures/ai-eval/v1/synthetic-pass.json");
    QFile source(sourceTrace);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QJsonObject trace = QJsonDocument::fromJson(source.readAll()).object();
    QJsonArray cases = trace.value(QStringLiteral("cases")).toArray();
    QJsonObject first = cases.at(0).toObject();
    first.insert(QStringLiteral("evidence"), QJsonArray{});
    cases.replace(0, first);
    trace.insert(QStringLiteral("cases"), cases);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile mutated(directory.filePath(QStringLiteral("trace.json")));
    QVERIFY(mutated.open(QIODevice::WriteOnly));
    QCOMPARE(mutated.write(QJsonDocument(trace).toJson(QJsonDocument::Compact)),
             QJsonDocument(trace).toJson(QJsonDocument::Compact).size());
    mutated.close();
    const auto result = ztermy::ai::AiEvaluationHarness::replay(corpus, mutated.fileName());
    QVERIFY(result.has_value());
    QVERIFY(!result->value(QStringLiteral("all_passed")).toBool());
    QCOMPARE(result->value(QStringLiteral("passed")).toInt(), 11);
}
} // namespace

QTEST_GUILESS_MAIN(AiEvaluationHarnessTests)

#include "ai_evaluation_harness_tests.moc"
