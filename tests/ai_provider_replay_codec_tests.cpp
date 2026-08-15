#include "domain/ai/AiProviderReplayCodec.h"

#include <QJsonDocument>
#include <QtTest/QTest>

#include <vector>

namespace
{

using ztermy::ai::AiProviderReplayCodec;
using ztermy::ai::AiProviderReplayError;
using ztermy::ai::AiToolCall;
using ztermy::ai::AiToolExchange;
using ztermy::ai::AiToolOutput;

class AiProviderReplayCodecTests final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsOpaqueProviderState();
    void rejectsMalformedAndOversizedState();
};

void AiProviderReplayCodecTests::roundTripsOpaqueProviderState()
{
    const std::vector history{AiToolExchange{
        .calls = {AiToolCall{.id = "tool_1", .name = "run_command", .argumentsJson = R"({"command":"df -h"})"}},
        .outputs = {AiToolOutput{.callId = "tool_1", .name = "run_command", .outputJson = R"({"ok":true})"}},
        .reasoning = "provider reasoning",
        .reasoningSignature = "signed-reasoning",
        .providerAssistantContentJson =
            R"json([{"type":"server_tool_use","id":"srvtoolu_1","name":"web_search","input":{"query":"Qt"}},{"type":"web_search_tool_result","tool_use_id":"srvtoolu_1","content":[{"type":"web_search_result","url":"https://example.test","encrypted_content":"opaque"}]}])json"}};
    constexpr std::string_view finalContent = R"json([{"type":"text","text":"The command completed."}])json";

    const auto encoded = AiProviderReplayCodec::encode(history, finalContent);
    QVERIFY(encoded.has_value());
    QVERIFY(encoded->size() <= AiProviderReplayCodec::maximumBytes);
    const auto decoded = AiProviderReplayCodec::decode(*encoded);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->toolHistory.size(), std::size_t{1});
    QCOMPARE(decoded->toolHistory.front().calls, history.front().calls);
    QCOMPARE(decoded->toolHistory.front().outputs, history.front().outputs);
    QCOMPARE(decoded->toolHistory.front().reasoning, history.front().reasoning);
    QCOMPARE(decoded->toolHistory.front().reasoningSignature, history.front().reasoningSignature);
    QCOMPARE(
        QJsonDocument::fromJson(QByteArray::fromStdString(decoded->toolHistory.front().providerAssistantContentJson)),
        QJsonDocument::fromJson(QByteArray::fromStdString(history.front().providerAssistantContentJson)));
    QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(decoded->finalAssistantContentJson)),
             QJsonDocument::fromJson(QByteArray(finalContent.data(), static_cast<qsizetype>(finalContent.size()))));
}

void AiProviderReplayCodecTests::rejectsMalformedAndOversizedState()
{
    const auto malformed = AiProviderReplayCodec::decode(R"({"version":1,"toolHistory":{}})");
    QVERIFY(!malformed.has_value());
    QCOMPARE(malformed.error(), AiProviderReplayError::invalidData);

    QVERIFY(!AiProviderReplayCodec::decode(R"({"version":1,"toolHistory":[{}]})").has_value());
    QVERIFY(
        !AiProviderReplayCodec::decode(
             R"({"version":1,"toolHistory":[{"calls":[{"id":"tool_1","name":"run_command","arguments":{}}],"outputs":[],"reasoning":"","reasoningSignature":""}]})")
             .has_value());

    const auto invalidContent = AiProviderReplayCodec::encode({}, R"({"type":"text"})");
    QVERIFY(!invalidContent.has_value());
    QCOMPARE(invalidContent.error(), AiProviderReplayError::invalidData);

    const std::vector<AiToolExchange> tooManyExchanges(65);
    const auto oversized = AiProviderReplayCodec::encode(tooManyExchanges, {});
    QVERIFY(!oversized.has_value());
    QCOMPARE(oversized.error(), AiProviderReplayError::limitExceeded);
}

} // namespace

QTEST_GUILESS_MAIN(AiProviderReplayCodecTests)

#include "ai_provider_replay_codec_tests.moc"
