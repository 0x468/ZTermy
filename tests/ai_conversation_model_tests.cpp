#include "application/ai/AiConversationModel.h"

#include "domain/ai/AiProviderReplayCodec.h"
#include "domain/ai/AiToolEvidence.h"

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
using ztermy::ai::AiProviderReplayCodec;
using ztermy::ai::AiTokenUsage;
using ztermy::ai::AiToolActivity;
using ztermy::ai::AiToolCall;
using ztermy::ai::AiToolEvidenceState;
using ztermy::ai::AiToolExchange;
using ztermy::ai::AiToolOutput;
using ztermy::ai::AiTurnMetrics;
using ztermy::ai::AiWebSource;
using ztermy::ai::evaluateToolEvidence;

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
    void exposesDeduplicatesAndRestoresWebSources();
    void preservesBoundedProviderReplayAcrossRestore();
    void preservesAgentTurnPresentationAcrossRestore();
    void classifiesAndRestoresToolEvidence();
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
    QCOMPARE(values.constFirst().toMap().value(QStringLiteral("fileName")).toString(), QStringLiteral("terminal.png"));
    QVERIFY(values.constFirst()
                .toMap()
                .value(QStringLiteral("previewUrl"))
                .toString()
                .startsWith(QStringLiteral("data:image/png;base64,")));

    const auto assistant = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistant, QStringLiteral("It is a terminal screenshot.")));
    QVERIFY(model.completeAssistantMessage(assistant));
    static_cast<void>(model.appendUserMessage(QStringLiteral("What should I do next?")));

    providerMessages = model.providerMessagesWithEvidence();
    QCOMPARE(providerMessages.size(), std::size_t{3});
    QVERIFY(providerMessages.front().images.empty());
    QVERIFY(QString::fromUtf8(providerMessages.front().content)
                .contains(QStringLiteral("Historical image attachment omitted from replay")));
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
    QVERIFY(model.setAssistantToolDetails(assistantId, QStringLiteral("call-1"),
                                          QStringLiteral("{\n  \"command\": \"df -h\"\n}"),
                                          QStringLiteral("{\n  \"ok\": true\n}")));
    const QVariantList activities = model.data(model.index(0), AiConversationModel::ToolActivitiesRole).toList();
    QCOMPARE(activities.size(), 1);
    const QVariantMap activity = activities.constFirst().toMap();
    QCOMPARE(activity.value(QStringLiteral("name")).toString(), QStringLiteral("run_command"));
    QCOMPARE(activity.value(QStringLiteral("summary")).toString(), QStringLiteral("df -h"));
    QCOMPARE(activity.value(QStringLiteral("argumentsJson")).toString(),
             QStringLiteral("{\n  \"command\": \"df -h\"\n}"));
    QCOMPARE(activity.value(QStringLiteral("resultJson")).toString(), QStringLiteral("{\n  \"ok\": true\n}"));
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

void AiConversationModelTests::exposesDeduplicatesAndRestoresWebSources()
{
    AiConversationModel model;
    const auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("Current information with citations.")));
    QVERIFY(model.appendAssistantSource(assistantId, AiWebSource{.url = "https://example.test/reference"}));
    QVERIFY(model.appendAssistantSource(assistantId, AiWebSource{.url = "https://example.test/reference",
                                                                 .title = "Primary reference",
                                                                 .citedText = "The cited passage."}));
    QVERIFY(!model.appendAssistantSource(assistantId, AiWebSource{.url = "file:///C:/private.txt"}));
    QVERIFY(model.completeAssistantMessage(assistantId));

    const QVariantList sources = model.data(model.index(0), AiConversationModel::SourcesRole).toList();
    QCOMPARE(sources.size(), 1);
    QCOMPARE(sources.constFirst().toMap().value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.test/reference"));
    QCOMPARE(sources.constFirst().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Primary reference"));
    QCOMPARE(sources.constFirst().toMap().value(QStringLiteral("citedText")).toString(),
             QStringLiteral("The cited passage."));

    const auto transcript = model.transcript();
    QCOMPARE(transcript.size(), std::size_t{1});
    QCOMPARE(transcript.front().sources.size(), std::size_t{1});

    AiConversationModel restored;
    QVERIFY(restored.restoreTranscript(transcript));
    QCOMPARE(restored.transcript(), transcript);
    QCOMPARE(restored.data(restored.index(0), AiConversationModel::SourcesRole).toList(), sources);

    auto invalidRole = transcript;
    invalidRole.front().role = AiConversationTranscriptRole::user;
    QVERIFY(!restored.restoreTranscript(invalidRole));
    QCOMPARE(restored.transcript(), transcript);
}

void AiConversationModelTests::preservesBoundedProviderReplayAcrossRestore()
{
    AiConversationModel model;
    static_cast<void>(model.appendUserMessage(QStringLiteral("Inspect disk usage")));
    const auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("Disk usage is healthy.")));
    const std::vector history{AiToolExchange{
        .calls = {AiToolCall{.id = "tool_1", .name = "run_command", .argumentsJson = R"({"command":"df -h"})"}},
        .outputs = {AiToolOutput{.callId = "tool_1", .name = "run_command", .outputJson = R"({"ok":true})"}}}};
    QVERIFY(
        model.setAssistantProviderReplay(assistantId, history, R"([{"type":"text","text":"Disk usage is healthy."}])"));
    QVERIFY(model.completeAssistantMessage(assistantId));

    const auto messages = model.providerMessages();
    QCOMPARE(messages.size(), std::size_t{2});
    QVERIFY(!messages.back().providerReplayJson.empty());
    const auto transcript = model.transcript();
    QCOMPARE(transcript.back().providerReplayJson, messages.back().providerReplayJson);

    AiConversationModel restored;
    QVERIFY(restored.restoreTranscript(transcript));
    QCOMPARE(restored.providerMessages().back().providerReplayJson, messages.back().providerReplayJson);
    QCOMPARE(restored.transcript(), transcript);

    auto invalid = transcript;
    invalid.back().providerReplayJson = "{}";
    QVERIFY(!restored.restoreTranscript(invalid));
    QCOMPARE(restored.transcript(), transcript);

    AiConversationModel bounded(
        AiConversationLimits{.maxMessages = 4, .maxMessageBytes = 32, .maxConversationBytes = 64});
    const auto boundedAssistant = bounded.beginAssistantMessage();
    QVERIFY(bounded.appendAssistantDelta(boundedAssistant, QStringLiteral("visible answer")));
    QVERIFY(
        !bounded.setAssistantProviderReplay(boundedAssistant, history, R"([{"type":"text","text":"visible answer"}])"));
    QVERIFY(bounded.data(bounded.index(0), AiConversationModel::TruncatedRole).toBool());
    QVERIFY(bounded.completeAssistantMessage(boundedAssistant));
    QCOMPARE(bounded.data(bounded.index(0), AiConversationModel::TextRole).toString(),
             QStringLiteral("visible answer"));

    const auto replayEnvelope =
        AiProviderReplayCodec::encode(history, R"([{"type":"text","text":"visible answer retained"}])");
    QVERIFY(replayEnvelope.has_value());
    AiConversationModel prioritized(AiConversationLimits{.maxMessages = 6,
                                                         .maxMessageBytes = replayEnvelope->size() + 64,
                                                         .maxConversationBytes = 2 * (replayEnvelope->size() + 32)});
    for (int index = 0; index < 3; ++index)
    {
        const auto id = prioritized.beginAssistantMessage();
        QVERIFY(prioritized.appendAssistantDelta(id, QStringLiteral("visible answer %1").arg(index)));
        QVERIFY(prioritized.setAssistantProviderReplay(id, history,
                                                       R"([{"type":"text","text":"visible answer retained"}])"));
        QVERIFY(prioritized.completeAssistantMessage(id));
    }
    QCOMPARE(prioritized.rowCount(), 3);
    const auto prioritizedMessages = prioritized.providerMessages();
    QCOMPARE(prioritizedMessages.size(), std::size_t{3});
    QVERIFY(prioritizedMessages.front().providerReplayJson.empty());
    QVERIFY(!prioritizedMessages.back().providerReplayJson.empty());
    QVERIFY(prioritized.data(prioritized.index(0), AiConversationModel::TruncatedRole).toBool());
}

void AiConversationModelTests::preservesAgentTurnPresentationAcrossRestore()
{
    AiConversationModel model;
    static_cast<void>(model.appendUserMessage(QStringLiteral("Inspect disk usage")));
    const auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantReasoningDelta(assistantId, QStringLiteral("I should inspect the mounted volumes.")));
    QVERIFY(model.upsertAssistantToolActivity(assistantId, QStringLiteral("tool-1"), QStringLiteral("run_command"),
                                              QStringLiteral("df -h"), QStringLiteral("succeeded"),
                                              QStringLiteral("ok"), true, false));
    QVERIFY(model.setAssistantToolDetails(assistantId, QStringLiteral("tool-1"),
                                          QStringLiteral("{\"command\":\"df -h\"}"),
                                          QStringLiteral("{\"ok\":true,\"output\":\"disk table\"}")));
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("Disk usage is healthy.")));
    QVERIFY(model.completeAssistantMessage(
        assistantId, AiTokenUsage{.inputTokens = 24, .outputTokens = 8, .reasoningTokens = 5, .cachedInputTokens = 3}));
    QVERIFY(model.setAssistantMetrics(
        assistantId, AiTurnMetrics{.wallTimeMilliseconds = 740, .firstTokenMilliseconds = 130, .retryCount = 1},
        AiCostEstimate{.usd = 0.00042, .catalogDate = "2026-08-15", .longContextRatesApplied = true}));

    auto transcript = model.transcript();
    QCOMPARE(transcript.size(), std::size_t{2});
    transcript.back().truncated = true;
    QCOMPARE(transcript.back().reasoning, std::string("I should inspect the mounted volumes."));
    QCOMPARE(transcript.back().toolActivities.size(), std::size_t{1});
    QVERIFY(transcript.back().usage.has_value());
    QVERIFY(transcript.back().metrics.has_value());
    QVERIFY(transcript.back().estimatedCostUsd.has_value());

    AiConversationModel restored;
    QVERIFY(restored.restoreTranscript(transcript));
    QCOMPARE(restored.transcript(), transcript);
    QCOMPARE(restored.data(restored.index(1), AiConversationModel::ReasoningRole).toString(),
             QStringLiteral("I should inspect the mounted volumes."));
    const QVariantList activities = restored.data(restored.index(1), AiConversationModel::ToolActivitiesRole).toList();
    QCOMPARE(activities.size(), 1);
    QCOMPARE(activities.constFirst().toMap().value(QStringLiteral("summary")).toString(), QStringLiteral("df -h"));
    QCOMPARE(activities.constFirst().toMap().value(QStringLiteral("argumentsJson")).toString(),
             QStringLiteral("{\"command\":\"df -h\"}"));
    QCOMPARE(activities.constFirst().toMap().value(QStringLiteral("resultJson")).toString(),
             QStringLiteral("{\"ok\":true,\"output\":\"disk table\"}"));
    QCOMPARE(restored.data(restored.index(1), AiConversationModel::InputTokensRole).toULongLong(), qulonglong{24});
    QCOMPARE(restored.data(restored.index(1), AiConversationModel::WallTimeMillisecondsRole).toULongLong(),
             qulonglong{740});
    QCOMPARE(restored.data(restored.index(1), AiConversationModel::EstimatedCostUsdRole).toDouble(), 0.00042);
    QVERIFY(restored.data(restored.index(1), AiConversationModel::LongContextRatesRole).toBool());
    QVERIFY(restored.data(restored.index(1), AiConversationModel::TruncatedRole).toBool());
}

void AiConversationModelTests::classifiesAndRestoresToolEvidence()
{
    QCOMPARE(evaluateToolEvidence(std::span<const AiToolActivity>{}).state, AiToolEvidenceState::none);
    const std::vector activities{
        AiToolActivity{.id = "read-1", .name = "read_terminal_frame", .state = "succeeded", .resultCode = "ok"},
        AiToolActivity{.id = "run-1",
                       .name = "run_command",
                       .state = "cancelled",
                       .resultCode = "permission_denied",
                       .sideEffecting = true}};
    const auto incomplete = evaluateToolEvidence(activities);
    QCOMPARE(incomplete.state, AiToolEvidenceState::incomplete);
    QCOMPARE(incomplete.succeededCount, std::uint32_t{1});
    QCOMPARE(incomplete.failedCount, std::uint32_t{1});
    QCOMPARE(incomplete.failedSideEffectCount, std::uint32_t{1});

    AiConversationModel model;
    const auto assistantId = model.beginAssistantMessage();
    QVERIFY(model.appendAssistantDelta(assistantId, QStringLiteral("The command is ready.")));
    QVERIFY(model.upsertAssistantToolActivity(assistantId, QStringLiteral("run-1"), QStringLiteral("run_command"),
                                              QStringLiteral("df -h"), QStringLiteral("awaiting_approval"),
                                              QStringLiteral("pending"), true, false));
    QVERIFY(model.completeAssistantMessage(assistantId));
    QCOMPARE(model.data(model.index(0), AiConversationModel::ToolEvidenceStateRole).toString(),
             QStringLiteral("pending"));
    QCOMPARE(model.data(model.index(0), AiConversationModel::ToolEvidencePendingCountRole).toUInt(), std::uint32_t{1});

    QVERIFY(model.upsertAssistantToolActivity(assistantId, QStringLiteral("run-1"), QStringLiteral("run_command"),
                                              QStringLiteral("df -h"), QStringLiteral("failed"),
                                              QStringLiteral("timeout"), true, false));
    QCOMPARE(model.data(model.index(0), AiConversationModel::ToolEvidenceStateRole).toString(),
             QStringLiteral("incomplete"));
    QCOMPARE(model.data(model.index(0), AiConversationModel::ToolEvidenceFailedCountRole).toUInt(), std::uint32_t{1});
    QCOMPARE(model.data(model.index(0), AiConversationModel::ToolEvidenceFailedSideEffectCountRole).toUInt(),
             std::uint32_t{1});

    AiConversationModel restored;
    QVERIFY(restored.restoreTranscript(model.transcript()));
    QCOMPARE(restored.data(restored.index(0), AiConversationModel::ToolEvidenceStateRole).toString(),
             QStringLiteral("incomplete"));
    QCOMPARE(restored.data(restored.index(0), AiConversationModel::ToolEvidenceFailedSideEffectCountRole).toUInt(),
             std::uint32_t{1});
}

} // namespace

QTEST_GUILESS_MAIN(AiConversationModelTests)

#include "ai_conversation_model_tests.moc"
