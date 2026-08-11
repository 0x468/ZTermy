#include "infrastructure/ai/ProviderRequestFactory.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <string>

namespace
{

using ztermy::ai::AiChatMessage;
using ztermy::ai::AiGenerationRequest;
using ztermy::ai::AiMessageRole;
using ztermy::ai::AiProviderConfiguration;
using ztermy::ai::AiProviderErrorCode;
using ztermy::ai::AiProviderKind;
using ztermy::ai::AiToolCall;
using ztermy::ai::AiToolDefinition;
using ztermy::ai::AiToolExchange;
using ztermy::ai::AiToolOutput;
using ztermy::ai::AiWireProtocol;
using ztermy::ai::ProviderRequestFactory;

class ProviderRequestFactoryTests final : public QObject
{
    Q_OBJECT

private slots:
    void preparesOpenAiResponsesRequest();
    void preparesOllamaRequest();
    void preparesCompatibleRequest();
    void rejectsUnsafeOrIncompleteConfiguration();
};

void ProviderRequestFactoryTests::preparesOpenAiResponsesRequest()
{
    const AiProviderConfiguration configuration{.kind = AiProviderKind::openAiResponses,
                                                .baseUrl = "https://api.openai.com/v1/",
                                                .model = "gpt-5.6"};
    const AiGenerationRequest generation{
        .instructions = "Use terminal evidence.",
        .messages = {AiChatMessage{.role = AiMessageRole::user, .content = "解释失败"}},
        .tools = {AiToolDefinition{.name = "read_terminal",
                                   .description = "Read terminal lines.",
                                   .parametersJson =
                                       R"({"type":"object","properties":{},"additionalProperties":false})"}},
        .toolHistory = {AiToolExchange{
            .outputs = {AiToolOutput{.callId = "call_1", .name = "read_terminal", .outputJson = R"({"ok":true})"}}}},
        .previousResponseId = "resp_previous"};
    const auto prepared = ProviderRequestFactory::prepare(configuration, generation, "test-secret");
    QVERIFY(prepared.has_value());
    QCOMPARE(prepared->request.url().toString(), QStringLiteral("https://api.openai.com/v1/responses"));
    QCOMPARE(prepared->request.rawHeader("Accept"), QByteArray("text/event-stream"));
    QCOMPARE(prepared->request.rawHeader("Authorization"), QByteArray("Bearer test-secret"));
    QCOMPARE(prepared->protocol, AiWireProtocol::serverSentEvents);

    const auto body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("model").toString(), QStringLiteral("gpt-5.6"));
    QCOMPARE(body.value("store").toBool(), false);
    QCOMPARE(body.value("previous_response_id").toString(), QStringLiteral("resp_previous"));
    QCOMPARE(body.value("input").toArray().first().toObject().value("type").toString(),
             QStringLiteral("function_call_output"));
    QCOMPARE(body.value("tools").toArray().first().toObject().value("name").toString(),
             QStringLiteral("read_terminal"));
}

void ProviderRequestFactoryTests::preparesOllamaRequest()
{
    const AiProviderConfiguration configuration{.kind = AiProviderKind::ollama,
                                                .baseUrl = "http://127.0.0.1:11434",
                                                .model = "qwen3"};
    const AiGenerationRequest generation{
        .instructions = "Be concise.",
        .messages = {AiChatMessage{.content = "hello"}},
        .tools = {AiToolDefinition{.name = "list_sessions",
                                   .description = "List sessions.",
                                   .parametersJson = R"({"type":"object","properties":{}})"}},
        .toolHistory = {AiToolExchange{
            .calls = {AiToolCall{.id = "call_1", .name = "list_sessions", .argumentsJson = "{}"}},
            .outputs = {AiToolOutput{.callId = "call_1", .name = "list_sessions", .outputJson = "[]"}}}}};
    const auto prepared = ProviderRequestFactory::prepare(configuration, generation, {});
    QVERIFY(prepared.has_value());
    QCOMPARE(prepared->request.url().toString(), QStringLiteral("http://127.0.0.1:11434/api/chat"));
    QCOMPARE(prepared->protocol, AiWireProtocol::ndjson);
    QCOMPARE(prepared->request.rawHeader("Authorization"), QByteArray{});
    const auto input = QJsonDocument::fromJson(prepared->body).object().value("messages").toArray();
    QCOMPARE(input.size(), 4);
    QCOMPARE(input.first().toObject().value("role").toString(), QStringLiteral("system"));
    const auto body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("tools").toArray().first().toObject().value("function").toObject().value("name").toString(),
             QStringLiteral("list_sessions"));
    QCOMPARE(input.last().toObject().value("role").toString(), QStringLiteral("tool"));
}

void ProviderRequestFactoryTests::preparesCompatibleRequest()
{
    const AiProviderConfiguration configuration{.kind = AiProviderKind::openAiCompatible,
                                                .baseUrl = "https://example.test/v1",
                                                .endpointPath = "custom/chat",
                                                .model = "model"};
    const auto prepared = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, "key");
    QVERIFY(prepared.has_value());
    QCOMPARE(prepared->request.url().toString(), QStringLiteral("https://example.test/v1/custom/chat"));
    const auto body = QJsonDocument::fromJson(prepared->body).object();
    QVERIFY(body.value("stream_options").toObject().value("include_usage").toBool());

    const AiGenerationRequest invalidGeneration{.tools = {AiToolDefinition{.name = "broken", .parametersJson = "[]"}}};
    const auto invalidTool = ProviderRequestFactory::prepare(configuration, invalidGeneration, "key");
    QVERIFY(!invalidTool.has_value());
    QCOMPARE(invalidTool.error().code, AiProviderErrorCode::invalidRequest);
}

void ProviderRequestFactoryTests::rejectsUnsafeOrIncompleteConfiguration()
{
    auto configuration = AiProviderConfiguration{.baseUrl = "https://user:pass@example.test/v1", .model = "model"};
    auto result = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, {});
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, AiProviderErrorCode::invalidRequest);

    configuration = AiProviderConfiguration{.baseUrl = "file:///tmp/provider", .model = "model"};
    result = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, {});
    QVERIFY(!result.has_value());

    configuration = AiProviderConfiguration{.baseUrl = "https://example.test/v1"};
    result = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, {});
    QVERIFY(!result.has_value());
}

} // namespace

QTEST_GUILESS_MAIN(ProviderRequestFactoryTests)

#include "provider_request_factory_tests.moc"
