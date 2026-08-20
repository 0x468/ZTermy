#include "domain/ai/AiContextCompactor.h"

#include <QtTest/QTest>

#include <string>

namespace
{

using ztermy::ai::AiChatMessage;
using ztermy::ai::AiCompactionLimits;
using ztermy::ai::AiContextCompactor;
using ztermy::ai::AiGenerationRequest;
using ztermy::ai::AiImageAttachment;

class AiContextCompactorTests final : public QObject
{
    Q_OBJECT

private slots:
    void leavesSmallRequestsUntouched();
    void truncatesOldMessagesAndPreservesRecentTail();
    void capsToolOutputsInHistory();
    void accountsForOpaqueProviderContinuationContent();
    void dropsOldProviderReplayBeforeVisibleHistory();
    void utf8TruncationNeverSplitsCodePoints();
    void accountsForImageTilesWithoutCountingBase64AsText();
};

void AiContextCompactorTests::leavesSmallRequestsUntouched()
{
    AiGenerationRequest request;
    request.instructions = "instructions";
    request.messages = {AiChatMessage{.role = ztermy::ai::AiMessageRole::user, .content = "hello"},
                        AiChatMessage{.role = ztermy::ai::AiMessageRole::assistant, .content = "hi"}};

    const auto result = AiContextCompactor::compact(std::move(request));
    QVERIFY(!result.compacted);
    QVERIFY(!result.overBudget);
    QCOMPARE(result.request.messages.size(), std::size_t{2});
    QCOMPARE(result.request.messages.back().content, std::string("hi"));
}

void AiContextCompactorTests::truncatesOldMessagesAndPreservesRecentTail()
{
    AiGenerationRequest request;
    const std::string longMessage(20'000, 'a');
    for (int index = 0; index < 30; ++index)
    {
        request.messages.push_back(AiChatMessage{.role = ztermy::ai::AiMessageRole::user, .content = longMessage});
    }

    const AiCompactionLimits limits{.contextWindowTokens = 128'000,
                                    .reservedOutputTokens = 16'000,
                                    .reserveBufferTokens = 8'000,
                                    .preserveRecentMessages = 10};
    const auto result = AiContextCompactor::compact(std::move(request), limits);
    QVERIFY(result.compacted);
    QVERIFY(!result.overBudget);

    // The twenty oldest messages were truncated; the recent ten stay verbatim.
    QCOMPARE(result.request.messages.size(), std::size_t{30});
    QVERIFY(result.request.messages[0].content.size() < 10'000);
    QVERIFY(result.request.messages[0].content.find("...[older content truncated]...") != std::string::npos);
    QVERIFY(result.request.messages[19].content.size() < 10'000);
    QCOMPARE(result.request.messages[29].content.size(), longMessage.size());
    QCOMPARE(result.compactedItemCount, std::size_t{20});
}

void AiContextCompactorTests::capsToolOutputsInHistory()
{
    AiGenerationRequest request;
    for (int index = 0; index < 10; ++index)
    {
        request.toolHistory.push_back(
            ztermy::ai::AiToolExchange{.calls = {ztermy::ai::AiToolCall{.id = "call_" + std::to_string(index),
                                                                        .name = "run_command",
                                                                        .argumentsJson = "{}"}},
                                       .outputs = {ztermy::ai::AiToolOutput{.callId = "call_" + std::to_string(index),
                                                                            .name = "run_command",
                                                                            .outputJson = std::string(50'000, 'b')}}});
    }

    const AiCompactionLimits limits{.contextWindowTokens = 128'000,
                                    .reservedOutputTokens = 16'000,
                                    .reserveBufferTokens = 8'000,
                                    .preserveRecentMessages = 10,
                                    .maximumToolOutputCharacters = 2'000};
    const auto result = AiContextCompactor::compact(std::move(request), limits);
    QVERIFY(result.compacted);
    QVERIFY(result.request.toolHistory.front().outputs.front().outputJson.size() < 3'000);
    QVERIFY(result.request.toolHistory.front().outputs.front().outputJson.find("...[truncated]...")
            != std::string::npos);
}

void AiContextCompactorTests::accountsForOpaqueProviderContinuationContent()
{
    AiGenerationRequest request;
    const std::string providerContent(4'000, 'p');
    request.toolHistory.push_back(ztermy::ai::AiToolExchange{.providerAssistantContentJson = providerContent});

    const auto estimate = AiContextCompactor::estimateRequestTokens(request);
    QVERIFY(estimate >= std::size_t{1'000});

    const AiCompactionLimits limits{.contextWindowTokens = 900,
                                    .reservedOutputTokens = 100,
                                    .reserveBufferTokens = 100};
    const auto result = AiContextCompactor::compact(std::move(request), limits);
    QVERIFY(result.overBudget);
    QCOMPARE(result.request.toolHistory.front().providerAssistantContentJson, providerContent);
}

void AiContextCompactorTests::dropsOldProviderReplayBeforeVisibleHistory()
{
    AiGenerationRequest request;
    for (int index = 0; index < 3; ++index)
    {
        request.messages.push_back(AiChatMessage{.role = ztermy::ai::AiMessageRole::assistant,
                                                 .content = "visible answer",
                                                 .providerReplayJson = std::string(1'000, 'r')});
    }
    const AiCompactionLimits limits{.contextWindowTokens = 800,
                                    .reservedOutputTokens = 100,
                                    .reserveBufferTokens = 100,
                                    .preserveRecentMessages = 1};
    const auto result = AiContextCompactor::compact(std::move(request), limits);
    QVERIFY(result.compacted);
    QVERIFY(!result.overBudget);
    QVERIFY(result.request.messages.at(0).providerReplayJson.empty());
    QVERIFY(result.request.messages.at(1).providerReplayJson.empty());
    QCOMPARE(result.request.messages.at(2).providerReplayJson.size(), std::size_t{1'000});
    QCOMPARE(result.request.messages.at(0).content, std::string("visible answer"));
    QCOMPARE(result.compactedItemCount, std::size_t{2});
    QVERIFY(result.removedBytes >= std::size_t{2'000});
}

void AiContextCompactorTests::utf8TruncationNeverSplitsCodePoints()
{
    AiGenerationRequest request;
    // 2000 repetitions of a 3-byte code point = 6000 bytes; truncation must
    // land on a code-point boundary so the payload stays valid UTF-8.
    const std::string utf8 = [] {
        std::string value;
        for (int index = 0; index < 2000; ++index)
        {
            value += "\xE4\xB8\xAD"; // U+4E2D (中) in UTF-8
        }
        return value;
    }();
    request.messages.push_back(AiChatMessage{.role = ztermy::ai::AiMessageRole::user, .content = utf8});

    const AiCompactionLimits limits{.contextWindowTokens = 1'000,
                                    .reservedOutputTokens = 100,
                                    .reserveBufferTokens = 100,
                                    .preserveRecentMessages = 0,
                                    .oldMessageHeadCharacters = 500,
                                    .oldMessageTailCharacters = 500};
    const auto result = AiContextCompactor::compact(std::move(request), limits);
    QVERIFY(result.compacted);
    const std::string &content = result.request.messages.front().content;
    const QString decoded = QString::fromUtf8(content.data(), static_cast<qsizetype>(content.size()));
    // A valid UTF-8 payload decodes without replacement characters.
    QVERIFY(!decoded.contains(QChar::ReplacementCharacter));
    QVERIFY(decoded.contains(QStringLiteral("...[older content truncated]...")));
}

void AiContextCompactorTests::accountsForImageTilesWithoutCountingBase64AsText()
{
    const AiGenerationRequest request{
        .messages = {AiChatMessage{.content = "inspect",
                                   .images = {AiImageAttachment{.mediaType = "image/png",
                                                                .base64Data = std::string(2'000'000, 'A'),
                                                                .pixelWidth = 1024,
                                                                .pixelHeight = 1024}}}}};
    const auto estimate = AiContextCompactor::estimateRequestTokens(request);
    QVERIFY(estimate >= std::size_t{765});
    QVERIFY(estimate < std::size_t{2'000});
}

} // namespace

QTEST_GUILESS_MAIN(AiContextCompactorTests)

#include "ai_context_compactor_tests.moc"
