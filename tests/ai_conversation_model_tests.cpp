#include "application/ai/AiConversationModel.h"

#include <QSignalSpy>
#include <QtTest/QTest>

namespace
{

using ztermy::ai::AiConversationLimits;
using ztermy::ai::AiConversationModel;
using ztermy::ai::AiConversationTranscriptEntry;
using ztermy::ai::AiConversationTranscriptRole;
using ztermy::ai::AiCostEstimate;
using ztermy::ai::AiImageAttachment;
using ztermy::ai::AiMessageRole;
using ztermy::ai::AiTokenUsage;
using ztermy::ai::AiTurnMetrics;

class AiConversationModelTests final : public QObject
{
    Q_OBJECT

private slots:
    void streamsAssistantMessageAndUsage();
    void preservesOnlyCurrentTurnImagePayloads();
    void boundsMessagesAndUtf8Text();
    void exposesFailureWithoutLeakingIntoLogs();
    void exposesCancellationAsRetryableNeutralState();
    void exposesBoundedNativeToolActivities();
    void exposesOnlyOneBoundedShellCommandSuggestion();
    void restoresOnlyBoundedTranscriptEntries();
    void preservesHiddenAgentEvidenceWithoutAddingVisibleRows();
    void preservesAgentEvidenceChronology();
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

void AiConversationModelTests::preservesOnlyCurrentTurnImagePayloads()
{
    AiConversationModel model;
    const AiImageAttachment image{.id = "image:test",
                                  .fileName = "terminal.png",
                                  .mediaType = "image/png",
                                  .base64Data = "aW1hZ2U=",
                                  .previewBase64Data = "cHJldmlldw==",
                                  .byteSize = 5,
                                  .pixelWidth = 640,
                                  .pixelHeight = 480};
    static_cast<void>(model.appendUserMessage(QStringLiteral("Inspect this"), {image}));

    auto providerMessages = model.providerMessagesWithEvidence();
    QCOMPARE(providerMessages.size(), std::size_t{1});
    QCOMPARE(providerMessages.front().images.size(), std::size_t{1});
    const QVariantList values = model.data(model.index(0), AiConversationModel::ImageAttachmentsRole).toList();
    QCOMPARE(values.size(), 1);
    QCOMPARE(values.constFirst().toMap().value(QStringLiteral("fileName")).toString(),
             QStringLiteral("terminal.png"));
    QVERIFY(values.constFirst().toMap().value(QStringLiteral("previewUrl")).toString().startsWith(
        QStringLiteral("data:image/png;base64,")));

    const auto assistant = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistant, QStringLiteral("It is a terminal screenshot.")));
    QVERIFY(model.completeAssistantMessage(assistant));
    static_cast<void>(model.appendUserMessage(QStringLiteral("What should I do next?")));

    providerMessages = model.providerMessagesWithEvidence();
    QCOMPARE(providerMessages.size(), std::size_t{3});
    QVERIFY(providerMessages.front().images.empty());
    QVERIFY(QString::fromUtf8(providerMessages.front().content).contains(
        QStringLiteral("Historical image attachment omitted from replay")));
    QVERIFY(model.transcript().front().content.contains("Historical image attachment omitted from replay"));
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

void AiConversationModelTests::restoresOnlyBoundedTranscriptEntries()
{
    AiConversationModel model;
    const std::vector<AiConversationTranscriptEntry> transcript{
        {.role = AiConversationTranscriptRole::user, .content = "question"},
        {.role = AiConversationTranscriptRole::assistant, .content = "answer"}};
    QVERIFY(model.restoreTranscript(transcript));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), AiConversationModel::TextRole).toString(), QStringLiteral("question"));
    QCOMPARE(model.data(model.index(1), AiConversationModel::TextRole).toString(), QStringLiteral("answer"));
    QVERIFY(!model.streaming());

    auto invalid = transcript;
    invalid.insert(invalid.begin(),
                   {.role = AiConversationTranscriptRole::evidence, .content = "unanchored tool result"});
    QVERIFY(!model.restoreTranscript(invalid));
    QCOMPARE(model.rowCount(), 2);
}

void AiConversationModelTests::preservesHiddenAgentEvidenceWithoutAddingVisibleRows()
{
    AiConversationModel model;
    static_cast<void>(model.appendUserMessage(QStringLiteral("check disks")));
    QVERIFY(model.appendEvidenceMessage(QStringLiteral("[Agent tool evidence]\nTool: run_command\nResult: df output")));
    QCOMPARE(model.rowCount(), 1);
    const auto transcript = model.transcript();
    QCOMPARE(transcript.size(), std::size_t{2});
    QCOMPARE(transcript.back().role, AiConversationTranscriptRole::evidence);
    QVERIFY(QString::fromUtf8(transcript.back().content).contains(QStringLiteral("df output")));

    AiConversationModel restored;
    QVERIFY(restored.restoreTranscript(transcript));
    QCOMPARE(restored.rowCount(), 1);
    QCOMPARE(restored.transcript(), transcript);
    restored.clear();
    QVERIFY(restored.transcript().empty());
}

void AiConversationModelTests::preservesAgentEvidenceChronology()
{
    AiConversationModel model;
    static_cast<void>(model.appendUserMessage(QStringLiteral("check disks")));
    auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("I will inspect disk usage.")));
    QVERIFY(model.completeAssistantMessage(assistantId));
    QVERIFY(model.appendEvidenceMessage(QStringLiteral("df output")));
    static_cast<void>(model.appendUserMessage(QStringLiteral("and memory?")));
    assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("I will inspect memory.")));
    QVERIFY(model.completeAssistantMessage(assistantId));
    QVERIFY(model.appendEvidenceMessage(QStringLiteral("free output")));
    static_cast<void>(model.appendUserMessage(QStringLiteral("summarize both")));

    const std::vector<AiConversationTranscriptEntry> expected{
        {.role = AiConversationTranscriptRole::user, .content = "check disks"},
        {.role = AiConversationTranscriptRole::assistant, .content = "I will inspect disk usage."},
        {.role = AiConversationTranscriptRole::evidence, .content = "df output"},
        {.role = AiConversationTranscriptRole::user, .content = "and memory?"},
        {.role = AiConversationTranscriptRole::assistant, .content = "I will inspect memory."},
        {.role = AiConversationTranscriptRole::evidence, .content = "free output"},
        {.role = AiConversationTranscriptRole::user, .content = "summarize both"}};
    QCOMPARE(model.transcript(), expected);

    const auto providerMessages = model.providerMessagesWithEvidence();
    QCOMPARE(providerMessages.size(), expected.size());
    QCOMPARE(providerMessages.at(1).role, AiMessageRole::assistant);
    QCOMPARE(providerMessages.at(2).role, AiMessageRole::user);
    QCOMPARE(providerMessages.at(2).content, std::string("df output"));
    QCOMPARE(providerMessages.at(5).content, std::string("free output"));
    QCOMPARE(model.providerMessages().size(), std::size_t{5});

    AiConversationModel restored;
    QVERIFY(restored.restoreTranscript(expected));
    QCOMPARE(restored.transcript(), expected);
}

} // namespace

QTEST_GUILESS_MAIN(AiConversationModelTests)

#include "ai_conversation_model_tests.moc"
