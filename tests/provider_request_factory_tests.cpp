#include "infrastructure/ai/ProviderModelCatalog.h"
#include "infrastructure/ai/ProviderRequestFactory.h"

#include "domain/ai/AiProviderReplayCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <string>

namespace
{

using ztermy::ai::AiChatMessage;
using ztermy::ai::AiGenerationRequest;
using ztermy::ai::AiImageAttachment;
using ztermy::ai::AiMessageRole;
using ztermy::ai::AiProviderConfiguration;
using ztermy::ai::AiProviderErrorCode;
using ztermy::ai::AiProviderFlavor;
using ztermy::ai::AiProviderKind;
using ztermy::ai::AiReasoningEffort;
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
    void preparesAnthropicRequest();
    void preparesProviderNativeMultimodalRequests();
    void preparesProviderNativeWebSearchRequests();
    void appliesProviderSpecificReasoningControls();
    void preservesAnthropicThinkingSignatureAcrossTools();
    void replaysOpaqueAnthropicAssistantContent();
    void replaysPersistedAnthropicTurnInMessageOrder();
    void resolvesFriendlyApiAddressesAndModels();
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
        .tools = {AiToolDefinition{.name = "read_terminal_info",
                                   .description = "Read the current terminal session.",
                                   .parametersJson = R"({"type":"object","properties":{}})"}},
        .toolHistory = {AiToolExchange{
            .calls = {AiToolCall{.id = "call_1", .name = "read_terminal_info", .argumentsJson = "{}"}},
            .outputs = {AiToolOutput{.callId = "call_1", .name = "read_terminal_info", .outputJson = "[]"}}}}};
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
             QStringLiteral("read_terminal_info"));
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

    const AiGenerationRequest replayGeneration{
        .toolHistory = {AiToolExchange{
            .calls = {AiToolCall{.id = "call_1",
                                 .name = "run_command",
                                 .argumentsJson = R"({"command":"df -h"})",
                                 .providerDataJson = R"({"google":{"thought_signature":"opaque-signature"}})"}},
            .outputs = {AiToolOutput{.callId = "call_1", .name = "run_command", .outputJson = R"({"ok":true})"}}}}};
    const auto replayPrepared = ProviderRequestFactory::prepare(configuration, replayGeneration, "key");
    QVERIFY(replayPrepared.has_value());
    const auto replayMessages = QJsonDocument::fromJson(replayPrepared->body).object().value("messages").toArray();
    const auto replayCall = replayMessages.first().toObject().value("tool_calls").toArray().first().toObject();
    QCOMPARE(
        replayCall.value("extra_content").toObject().value("google").toObject().value("thought_signature").toString(),
        QStringLiteral("opaque-signature"));

    auto invalidReplay = replayGeneration;
    invalidReplay.toolHistory.front().calls.front().providerDataJson = "[]";
    const auto invalidMetadata = ProviderRequestFactory::prepare(configuration, invalidReplay, "key");
    QVERIFY(!invalidMetadata.has_value());
    QCOMPARE(invalidMetadata.error().code, AiProviderErrorCode::invalidRequest);

    const AiGenerationRequest invalidGeneration{.tools = {AiToolDefinition{.name = "broken", .parametersJson = "[]"}}};
    const auto invalidTool = ProviderRequestFactory::prepare(configuration, invalidGeneration, "key");
    QVERIFY(!invalidTool.has_value());
    QCOMPARE(invalidTool.error().code, AiProviderErrorCode::invalidRequest);
}

void ProviderRequestFactoryTests::preparesAnthropicRequest()
{
    const AiProviderConfiguration configuration{.kind = AiProviderKind::anthropicMessages,
                                                .baseUrl = "https://api.anthropic.com",
                                                .model = "claude-sonnet-4-6"};
    const AiGenerationRequest generation{
        .instructions = "Be concise.",
        .messages = {AiChatMessage{.content = "hello"}},
        .tools = {AiToolDefinition{.name = "run_command",
                                   .description = "Run a command.",
                                   .parametersJson = R"({"type":"object","properties":{}})"}}};
    const auto prepared = ProviderRequestFactory::prepare(configuration, generation, "anthropic-key");
    QVERIFY(prepared.has_value());
    QCOMPARE(prepared->request.url().toString(), QStringLiteral("https://api.anthropic.com/v1/messages"));
    QCOMPARE(prepared->request.rawHeader("x-api-key"), QByteArray("anthropic-key"));
    QCOMPARE(prepared->request.rawHeader("anthropic-version"), QByteArray("2023-06-01"));
    QVERIFY(prepared->request.rawHeader("Authorization").isEmpty());
    const auto body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("system").toString(), QStringLiteral("Be concise."));
    QCOMPARE(body.value("max_tokens").toInt(), 8192);
    QCOMPARE(body.value("tools").toArray().first().toObject().value("input_schema").toObject().value("type").toString(),
             QStringLiteral("object"));
}

void ProviderRequestFactoryTests::preparesProviderNativeMultimodalRequests()
{
    const AiImageAttachment image{.id = "image:test",
                                  .fileName = "terminal.png",
                                  .mediaType = "image/png",
                                  .base64Data = "aW1hZ2U=",
                                  .byteSize = 5,
                                  .pixelWidth = 640,
                                  .pixelHeight = 480};
    const AiGenerationRequest generation{
        .messages = {AiChatMessage{.role = AiMessageRole::user, .content = "Inspect this", .images = {image}}}};

    auto prepared = ProviderRequestFactory::prepare(AiProviderConfiguration{.kind = AiProviderKind::openAiResponses,
                                                                            .baseUrl = "https://api.openai.com/v1",
                                                                            .model = "gpt-5.6"},
                                                    generation, "key");
    QVERIFY(prepared.has_value());
    auto body = QJsonDocument::fromJson(prepared->body).object();
    auto content = body.value("input").toArray().first().toObject().value("content").toArray();
    QCOMPARE(content.size(), 2);
    QCOMPARE(content.at(0).toObject().value("type").toString(), QStringLiteral("input_text"));
    QCOMPARE(content.at(1).toObject().value("type").toString(), QStringLiteral("input_image"));
    QVERIFY(
        content.at(1).toObject().value("image_url").toString().startsWith(QStringLiteral("data:image/png;base64,")));

    prepared = ProviderRequestFactory::prepare(AiProviderConfiguration{.kind = AiProviderKind::openAiCompatible,
                                                                       .baseUrl = "https://gateway.example.test/v1",
                                                                       .model = "vision"},
                                               generation, "key");
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    content = body.value("messages").toArray().first().toObject().value("content").toArray();
    QCOMPARE(content.at(1).toObject().value("type").toString(), QStringLiteral("image_url"));
    QVERIFY(content.at(1)
                .toObject()
                .value("image_url")
                .toObject()
                .value("url")
                .toString()
                .startsWith(QStringLiteral("data:image/png;base64,")));

    prepared = ProviderRequestFactory::prepare(AiProviderConfiguration{.kind = AiProviderKind::anthropicMessages,
                                                                       .baseUrl = "https://api.anthropic.com",
                                                                       .model = "claude-sonnet-4-6"},
                                               generation, "key");
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    content = body.value("messages").toArray().first().toObject().value("content").toArray();
    const auto source = content.at(1).toObject().value("source").toObject();
    QCOMPARE(content.at(1).toObject().value("type").toString(), QStringLiteral("image"));
    QCOMPARE(source.value("type").toString(), QStringLiteral("base64"));
    QCOMPARE(source.value("media_type").toString(), QStringLiteral("image/png"));
    QCOMPARE(source.value("data").toString(), QStringLiteral("aW1hZ2U="));

    prepared = ProviderRequestFactory::prepare(
        AiProviderConfiguration{.kind = AiProviderKind::ollama, .baseUrl = "http://127.0.0.1:11434", .model = "gemma3"},
        generation, {});
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    const auto ollamaMessage = body.value("messages").toArray().first().toObject();
    QCOMPARE(ollamaMessage.value("content").toString(), QStringLiteral("Inspect this"));
    QCOMPARE(ollamaMessage.value("images").toArray().first().toString(), QStringLiteral("aW1hZ2U="));
}

void ProviderRequestFactoryTests::preparesProviderNativeWebSearchRequests()
{
    const AiGenerationRequest generation{.messages = {AiChatMessage{.content = "Find current documentation."}},
                                         .webSearchEnabled = true};

    auto prepared = ProviderRequestFactory::prepare(AiProviderConfiguration{.kind = AiProviderKind::openAiResponses,
                                                                            .baseUrl = "https://api.openai.com/v1",
                                                                            .model = "gpt-5.6"},
                                                    generation, "key");
    QVERIFY(prepared.has_value());
    auto tools = QJsonDocument::fromJson(prepared->body).object().value("tools").toArray();
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools.first().toObject().value("type").toString(), QStringLiteral("web_search"));

    prepared = ProviderRequestFactory::prepare(AiProviderConfiguration{.kind = AiProviderKind::anthropicMessages,
                                                                       .baseUrl = "https://api.anthropic.com",
                                                                       .model = "claude-sonnet-4-6"},
                                               generation, "key");
    QVERIFY(prepared.has_value());
    tools = QJsonDocument::fromJson(prepared->body).object().value("tools").toArray();
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools.first().toObject().value("type").toString(), QStringLiteral("web_search_20250305"));
    QCOMPARE(tools.first().toObject().value("name").toString(), QStringLiteral("web_search"));
    QCOMPARE(tools.first().toObject().value("max_uses").toInt(), 5);

    const auto unsupported = ProviderRequestFactory::prepare(
        AiProviderConfiguration{.kind = AiProviderKind::ollama, .baseUrl = "http://127.0.0.1:11434", .model = "qwen3"},
        generation, {});
    QVERIFY(!unsupported.has_value());
    QCOMPARE(unsupported.error().code, AiProviderErrorCode::invalidRequest);
}

void ProviderRequestFactoryTests::appliesProviderSpecificReasoningControls()
{
    auto configuration = AiProviderConfiguration{.kind = AiProviderKind::openAiCompatible,
                                                 .flavor = AiProviderFlavor::deepSeek,
                                                 .baseUrl = "https://api.deepseek.com",
                                                 .model = "deepseek-chat"};
    auto prepared = ProviderRequestFactory::prepare(
        configuration, AiGenerationRequest{.reasoningEffort = AiReasoningEffort::high}, "key");
    QVERIFY(prepared.has_value());
    auto body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("thinking").toObject().value("type").toString(), QStringLiteral("enabled"));
    QCOMPARE(body.value("reasoning_effort").toString(), QStringLiteral("high"));

    configuration.flavor = AiProviderFlavor::zai;
    prepared = ProviderRequestFactory::prepare(
        configuration, AiGenerationRequest{.reasoningEffort = AiReasoningEffort::disabled}, "key");
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("thinking").toObject().value("type").toString(), QStringLiteral("disabled"));
    QCOMPARE(body.value("thinking").toObject().value("clear_thinking").toBool(), false);

    configuration.flavor = AiProviderFlavor::kimi;
    configuration.model = "kimi-k3";
    prepared = ProviderRequestFactory::prepare(
        configuration, AiGenerationRequest{.reasoningEffort = AiReasoningEffort::maximum}, "key");
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("reasoning_effort").toString(), QStringLiteral("max"));

    configuration.flavor = AiProviderFlavor::gemini;
    prepared = ProviderRequestFactory::prepare(
        configuration, AiGenerationRequest{.reasoningEffort = AiReasoningEffort::medium}, "key");
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("reasoning_effort").toString(), QStringLiteral("medium"));

    configuration.flavor = AiProviderFlavor::openRouter;
    prepared = ProviderRequestFactory::prepare(
        configuration, AiGenerationRequest{.reasoningEffort = AiReasoningEffort::disabled}, "key");
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("reasoning_effort").toString(), QStringLiteral("none"));

    configuration.flavor = AiProviderFlavor::qwen;
    prepared = ProviderRequestFactory::prepare(
        configuration, AiGenerationRequest{.reasoningEffort = AiReasoningEffort::disabled}, "key");
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    QVERIFY(body.contains("enable_thinking"));
    QCOMPARE(body.value("enable_thinking").toBool(), false);

    configuration.model = "qwen3.8-max";
    prepared = ProviderRequestFactory::prepare(
        configuration, AiGenerationRequest{.reasoningEffort = AiReasoningEffort::automatic}, "key");
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    QVERIFY(body.contains("preserve_thinking"));
    QCOMPARE(body.value("preserve_thinking").toBool(), false);
    QVERIFY(!body.contains("enable_thinking"));

    configuration.kind = AiProviderKind::openAiResponses;
    configuration.flavor = AiProviderFlavor::openAi;
    configuration.baseUrl = "https://api.openai.com/v1";
    configuration.model = "gpt-5.6";
    prepared = ProviderRequestFactory::prepare(
        configuration, AiGenerationRequest{.reasoningEffort = AiReasoningEffort::maximum}, "key");
    QVERIFY(prepared.has_value());
    body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("reasoning").toObject().value("effort").toString(), QStringLiteral("xhigh"));
}

void ProviderRequestFactoryTests::preservesAnthropicThinkingSignatureAcrossTools()
{
    const AiProviderConfiguration configuration{.kind = AiProviderKind::anthropicMessages,
                                                .flavor = AiProviderFlavor::anthropic,
                                                .baseUrl = "https://api.anthropic.com",
                                                .model = "claude-sonnet-4-6"};
    const AiGenerationRequest generation{
        .toolHistory = {AiToolExchange{
            .calls = {AiToolCall{.id = "tool_1", .name = "run_command", .argumentsJson = R"({"command":"pwd"})"}},
            .outputs = {AiToolOutput{.callId = "tool_1", .name = "run_command", .outputJson = R"({"ok":true})"}},
            .reasoning = "private provider reasoning",
            .reasoningSignature = "signed-block"}},
        .reasoningEffort = AiReasoningEffort::high};
    const auto prepared = ProviderRequestFactory::prepare(configuration, generation, "key");
    QVERIFY(prepared.has_value());
    const auto body = QJsonDocument::fromJson(prepared->body).object();
    QCOMPARE(body.value("thinking").toObject().value("type").toString(), QStringLiteral("adaptive"));
    QCOMPARE(body.value("output_config").toObject().value("effort").toString(), QStringLiteral("high"));
    const auto assistantContent = body.value("messages").toArray().first().toObject().value("content").toArray();
    QCOMPARE(assistantContent.first().toObject().value("type").toString(), QStringLiteral("thinking"));
    QCOMPARE(assistantContent.first().toObject().value("signature").toString(), QStringLiteral("signed-block"));
}

void ProviderRequestFactoryTests::replaysOpaqueAnthropicAssistantContent()
{
    const AiProviderConfiguration configuration{.kind = AiProviderKind::anthropicMessages,
                                                .flavor = AiProviderFlavor::anthropic,
                                                .baseUrl = "https://api.anthropic.com",
                                                .model = "claude-sonnet-4-6"};
    const AiGenerationRequest generation{
        .messages = {AiChatMessage{.role = AiMessageRole::user, .content = "Search current Qt documentation."}},
        .toolHistory = {AiToolExchange{
            .providerAssistantContentJson =
                R"json([{"type":"server_tool_use","id":"srvtoolu_1","name":"web_search","input":{"query":"Qt 6.8"}},{"type":"web_search_tool_result","tool_use_id":"srvtoolu_1","content":[{"type":"web_search_result","url":"https://example.test","encrypted_content":"opaque"}]}])json"}},
        .webSearchEnabled = true};

    const auto prepared = ProviderRequestFactory::prepare(configuration, generation, "key");
    QVERIFY(prepared.has_value());
    const auto messages = QJsonDocument::fromJson(prepared->body).object().value("messages").toArray();
    QCOMPARE(messages.size(), 2);
    const auto replay = messages.at(1).toObject().value("content").toArray();
    QCOMPARE(replay.size(), 2);
    QCOMPARE(replay.at(0).toObject().value("type").toString(), QStringLiteral("server_tool_use"));
    QCOMPARE(replay.at(1).toObject().value("content").toArray().at(0).toObject().value("encrypted_content").toString(),
             QStringLiteral("opaque"));
}

void ProviderRequestFactoryTests::replaysPersistedAnthropicTurnInMessageOrder()
{
    const AiProviderConfiguration configuration{.kind = AiProviderKind::anthropicMessages,
                                                .flavor = AiProviderFlavor::anthropic,
                                                .baseUrl = "https://api.anthropic.com",
                                                .model = "claude-sonnet-4-6"};
    const std::vector history{AiToolExchange{
        .calls = {AiToolCall{.id = "tool_1", .name = "run_command", .argumentsJson = R"({"command":"pwd"})"}},
        .outputs = {AiToolOutput{.callId = "tool_1", .name = "run_command", .outputJson = R"({"cwd":"/home/test"})"}},
        .providerAssistantContentJson =
            R"([{"type":"tool_use","id":"tool_1","name":"run_command","input":{"command":"pwd"}}])"}};
    const auto replay = ztermy::ai::AiProviderReplayCodec::encode(
        history, R"([{"type":"text","text":"The working directory is /home/test."}])");
    QVERIFY(replay.has_value());
    const AiGenerationRequest generation{
        .messages = {AiChatMessage{.role = AiMessageRole::user, .content = "Where am I?"},
                     AiChatMessage{.role = AiMessageRole::assistant,
                                   .content = "The working directory is /home/test.",
                                   .providerReplayJson = *replay},
                     AiChatMessage{.role = AiMessageRole::user, .content = "List its files."}}};

    const auto prepared = ProviderRequestFactory::prepare(configuration, generation, "key");
    QVERIFY(prepared.has_value());
    const auto messages = QJsonDocument::fromJson(prepared->body).object().value("messages").toArray();
    QCOMPARE(messages.size(), 5);
    QCOMPARE(messages.at(0).toObject().value("role").toString(), QStringLiteral("user"));
    QCOMPARE(messages.at(1).toObject().value("content").toArray().at(0).toObject().value("type").toString(),
             QStringLiteral("tool_use"));
    QCOMPARE(messages.at(2).toObject().value("content").toArray().at(0).toObject().value("type").toString(),
             QStringLiteral("tool_result"));
    QCOMPARE(messages.at(3).toObject().value("content").toArray().at(0).toObject().value("text").toString(),
             QStringLiteral("The working directory is /home/test."));
    QCOMPARE(messages.at(4).toObject().value("content").toString(), QStringLiteral("List its files."));

    auto invalid = generation;
    invalid.messages.at(1).providerReplayJson = "{}";
    const auto rejected = ProviderRequestFactory::prepare(configuration, invalid, "key");
    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code, AiProviderErrorCode::invalidRequest);
}

void ProviderRequestFactoryTests::resolvesFriendlyApiAddressesAndModels()
{
    auto configuration = AiProviderConfiguration{.kind = AiProviderKind::openAiCompatible,
                                                 .baseUrl = "https://gateway.example.test",
                                                 .model = "model"};
    auto prepared = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, "key");
    QVERIFY(prepared.has_value());
    QCOMPARE(prepared->request.url().toString(), QStringLiteral("https://gateway.example.test/v1/chat/completions"));

    configuration.baseUrl = "https://gateway.example.test/custom/v4";
    prepared = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, "key");
    QCOMPARE(prepared->request.url().toString(),
             QStringLiteral("https://gateway.example.test/custom/v4/chat/completions"));

    configuration.baseUrl = "https://gateway.example.test/custom/";
    prepared = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, "key");
    QCOMPARE(prepared->request.url().toString(),
             QStringLiteral("https://gateway.example.test/custom/chat/completions"));

    configuration.baseUrl = "https://gateway.example.test/custom/chat#";
    prepared = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, "key");
    QCOMPARE(prepared->request.url().toString(), QStringLiteral("https://gateway.example.test/custom/chat"));
    auto unavailableCatalog = ztermy::ai::ProviderModelCatalog::prepareRequest(
        configuration, ztermy::security::SensitiveByteArray(QByteArray("key")));
    QVERIFY(!unavailableCatalog.has_value());

    configuration.baseUrl = "https://gateway.example.test";
    auto catalogRequest = ztermy::ai::ProviderModelCatalog::prepareRequest(
        configuration, ztermy::security::SensitiveByteArray(QByteArray("key")));
    QVERIFY(catalogRequest.has_value());
    QCOMPARE(catalogRequest->url().toString(), QStringLiteral("https://gateway.example.test/v1/models"));
    const auto models = ztermy::ai::ProviderModelCatalog::parse(
        configuration.kind, R"({"data":[{"id":"zeta"},{"id":"alpha"},{"id":"alpha"}]})");
    QVERIFY(models.has_value());
    QCOMPARE(*models, QStringList({QStringLiteral("alpha"), QStringLiteral("zeta")}));

    configuration.baseUrl = "https://generativelanguage.googleapis.com/v1beta/openai/";
    prepared = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, "key");
    QVERIFY(prepared.has_value());
    QCOMPARE(prepared->request.url().toString(),
             QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"));
    catalogRequest = ztermy::ai::ProviderModelCatalog::prepareRequest(
        configuration, ztermy::security::SensitiveByteArray(QByteArray("key")));
    QVERIFY(catalogRequest.has_value());
    QCOMPARE(catalogRequest->url().toString(),
             QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/models"));

    configuration.baseUrl = "https://openrouter.ai/api/v1";
    prepared = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, "key");
    QVERIFY(prepared.has_value());
    QCOMPARE(prepared->request.url().toString(), QStringLiteral("https://openrouter.ai/api/v1/chat/completions"));
    catalogRequest = ztermy::ai::ProviderModelCatalog::prepareRequest(
        configuration, ztermy::security::SensitiveByteArray(QByteArray("key")));
    QVERIFY(catalogRequest.has_value());
    QCOMPARE(catalogRequest->url().toString(), QStringLiteral("https://openrouter.ai/api/v1/models"));

    configuration.baseUrl = "https://dashscope.aliyuncs.com/compatible-mode/v1";
    prepared = ProviderRequestFactory::prepare(configuration, AiGenerationRequest{}, "key");
    QVERIFY(prepared.has_value());
    QCOMPARE(prepared->request.url().toString(),
             QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"));
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
