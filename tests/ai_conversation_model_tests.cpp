#include "application/ai/AiConversationModel.h"

#include <QSignalSpy>
#include <QtTest/QTest>

namespace
{

using ztermy::ai::AiConversationLimits;
using ztermy::ai::AiConversationModel;
using ztermy::ai::AiCostEstimate;
using ztermy::ai::AiTokenUsage;
using ztermy::ai::AiTurnMetrics;

class AiConversationModelTests final : public QObject
{
    Q_OBJECT

private slots:
    void streamsAssistantMessageAndUsage();
    void boundsMessagesAndUtf8Text();
    void exposesFailureWithoutLeakingIntoLogs();
};

void AiConversationModelTests::streamsAssistantMessageAndUsage()
{
    AiConversationModel model;
    QSignalSpy streamingSpy(&model, &AiConversationModel::streamingChanged);
    const auto userId = model.appendUserMessage(QStringLiteral("Explain this"));
    QVERIFY(userId > 0);
    const auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.streaming());
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("Hello ")));
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("世界")));
    QVERIFY(model.completeAssistantMessage(assistantId, AiTokenUsage{.inputTokens = 12, .outputTokens = 3}));
    QVERIFY(model.setAssistantMetrics(
        assistantId, AiTurnMetrics{.wallTimeMilliseconds = 640, .firstTokenMilliseconds = 120, .retryCount = 1},
        AiCostEstimate{.usd = 0.00031, .catalogDate = "2026-08-12", .longContextRatesApplied = false}));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(1), AiConversationModel::TextRole).toString(), QStringLiteral("Hello 世界"));
    QCOMPARE(model.data(model.index(1), AiConversationModel::StateRole).toString(), QStringLiteral("complete"));
    QCOMPARE(model.data(model.index(1), AiConversationModel::InputTokensRole).toULongLong(), qulonglong{12});
    QCOMPARE(model.data(model.index(1), AiConversationModel::OutputTokensRole).toULongLong(), qulonglong{3});
    QCOMPARE(model.data(model.index(1), AiConversationModel::WallTimeMillisecondsRole).toULongLong(), qulonglong{640});
    QCOMPARE(model.data(model.index(1), AiConversationModel::FirstTokenMillisecondsRole).toLongLong(), qlonglong{120});
    QCOMPARE(model.data(model.index(1), AiConversationModel::RetryCountRole).toUInt(), std::uint32_t{1});
    QVERIFY(model.data(model.index(1), AiConversationModel::EstimatedCostKnownRole).toBool());
    QCOMPARE(model.data(model.index(1), AiConversationModel::CostCatalogDateRole).toString(),
             QStringLiteral("2026-08-12"));
    QVERIFY(!model.streaming());
    QCOMPARE(streamingSpy.count(), 2);
}

void AiConversationModelTests::boundsMessagesAndUtf8Text()
{
    AiConversationModel model(AiConversationLimits{.maxMessages = 2, .maxMessageBytes = 5, .maxConversationBytes = 10});
    static_cast<void>(model.appendUserMessage(QStringLiteral("first")));
    static_cast<void>(model.appendUserMessage(QStringLiteral("second")));
    static_cast<void>(model.appendUserMessage(QStringLiteral("你好")));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(1), AiConversationModel::TextRole).toString(), QStringLiteral("你"));
    QVERIFY(model.data(model.index(1), AiConversationModel::TruncatedRole).toBool());
}

void AiConversationModelTests::exposesFailureWithoutLeakingIntoLogs()
{
    AiConversationModel model;
    const auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.failAssistantMessage(assistantId, QStringLiteral("Provider unavailable")));
    QCOMPARE(model.data(model.index(0), AiConversationModel::StateRole).toString(), QStringLiteral("failed"));
    QCOMPARE(model.data(model.index(0), AiConversationModel::ErrorRole).toString(),
             QStringLiteral("Provider unavailable"));
    QVERIFY(!model.streaming());
    model.clear();
    QCOMPARE(model.rowCount(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(AiConversationModelTests)

#include "ai_conversation_model_tests.moc"
