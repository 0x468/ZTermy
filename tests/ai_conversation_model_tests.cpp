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
    void exposesCancellationAsRetryableNeutralState();
    void exposesBoundedNativeToolActivities();
    void exposesOnlyOneBoundedShellCommandSuggestion();
    void restoresOnlyBoundedUserAndAssistantMessages();
    void preservesHiddenAgentEvidenceWithoutAddingVisibleRows();
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

void AiConversationModelTests::exposesCancellationAsRetryableNeutralState()
{
    AiConversationModel model;
    static_cast<void>(model.appendUserMessage(QStringLiteral("continue")));
    const auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("partial")));
    QVERIFY(model.cancelAssistantMessage(assistantId));
    QCOMPARE(model.data(model.index(1), AiConversationModel::StateRole).toString(), QStringLiteral("cancelled"));
    QVERIFY(model.data(model.index(1), AiConversationModel::ErrorRole).toString().isEmpty());
    QCOMPARE(model.providerMessages().size(), std::size_t{1});
    QVERIFY(!model.streaming());
}

void AiConversationModelTests::exposesBoundedNativeToolActivities()
{
    AiConversationModel model;
    const auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.upsertAssistantToolActivity(assistantId, QStringLiteral("call-1"), QStringLiteral("run_command"),
                                              QStringLiteral("df -h"), QStringLiteral("queued"),
                                              QStringLiteral("pending"), true, false));
    QVERIFY(model.upsertAssistantToolActivity(assistantId, QStringLiteral("call-1"), QStringLiteral("run_command"),
                                              QStringLiteral("df -h"), QStringLiteral("succeeded"),
                                              QStringLiteral("ok"), true, false));
    const QVariantList activities = model.data(model.index(0), AiConversationModel::ToolActivitiesRole).toList();
    QCOMPARE(activities.size(), 1);
    const QVariantMap activity = activities.constFirst().toMap();
    QCOMPARE(activity.value(QStringLiteral("name")).toString(), QStringLiteral("run_command"));
    QCOMPARE(activity.value(QStringLiteral("summary")).toString(), QStringLiteral("df -h"));
    QCOMPARE(activity.value(QStringLiteral("state")).toString(), QStringLiteral("succeeded"));
}

void AiConversationModelTests::exposesOnlyOneBoundedShellCommandSuggestion()
{
    AiConversationModel model;
    auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("Use this:\n```pwsh\nGet-ChildItem -Force\n```")));
    QVERIFY(model.completeAssistantMessage(assistantId));
    QCOMPARE(model.data(model.index(0), AiConversationModel::CommandSuggestionRole).toString(),
             QStringLiteral("Get-ChildItem -Force"));
    QVERIFY(model.data(model.index(0), AiConversationModel::HasCommandSuggestionRole).toBool());

    assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("```sh\nls\n```\n```sh\npwd\n```")));
    QVERIFY(model.completeAssistantMessage(assistantId));
    QVERIFY(!model.data(model.index(1), AiConversationModel::HasCommandSuggestionRole).toBool());

    assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("No command block here.")));
    QVERIFY(model.completeAssistantMessage(assistantId));
    QVERIFY(!model.data(model.index(2), AiConversationModel::HasCommandSuggestionRole).toBool());
}

void AiConversationModelTests::restoresOnlyBoundedUserAndAssistantMessages()
{
    AiConversationModel model;
    const std::vector<ztermy::ai::AiChatMessage> transcript{
        {.role = ztermy::ai::AiMessageRole::user, .content = "question"},
        {.role = ztermy::ai::AiMessageRole::assistant, .content = "answer"}};
    QVERIFY(model.restoreProviderMessages(transcript));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), AiConversationModel::TextRole).toString(), QStringLiteral("question"));
    QCOMPARE(model.data(model.index(1), AiConversationModel::TextRole).toString(), QStringLiteral("answer"));
    QVERIFY(!model.streaming());

    auto invalid = transcript;
    invalid.push_back({.role = ztermy::ai::AiMessageRole::tool, .content = "untrusted tool replay"});
    QVERIFY(!model.restoreProviderMessages(invalid));
    QCOMPARE(model.rowCount(), 2);
}

void AiConversationModelTests::preservesHiddenAgentEvidenceWithoutAddingVisibleRows()
{
    AiConversationModel model;
    static_cast<void>(model.appendUserMessage(QStringLiteral("check disks")));
    QVERIFY(model.appendEvidenceMessage(QStringLiteral("[Agent tool evidence]\nTool: run_command\nResult: df output")));
    QCOMPARE(model.rowCount(), 1);
    const auto evidence = model.evidenceMessages();
    QCOMPARE(evidence.size(), std::size_t{1});
    QVERIFY(QString::fromUtf8(evidence.front().content).contains(QStringLiteral("df output")));

    AiConversationModel restored;
    QVERIFY(restored.restoreEvidenceMessages(evidence));
    QCOMPARE(restored.rowCount(), 0);
    const auto restoredEvidence = restored.evidenceMessages();
    QCOMPARE(restoredEvidence.size(), evidence.size());
    QCOMPARE(restoredEvidence.front().role, evidence.front().role);
    QCOMPARE(restoredEvidence.front().content, evidence.front().content);
    restored.clear();
    QVERIFY(restored.evidenceMessages().empty());
}

} // namespace

QTEST_GUILESS_MAIN(AiConversationModelTests)

#include "ai_conversation_model_tests.moc"
