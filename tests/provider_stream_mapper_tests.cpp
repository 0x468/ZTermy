#include "infrastructure/ai/ProviderStreamMapper.h"

#include <QTest>

#include <string>

namespace
{

using ztermy::ai::AiProviderErrorCode;
using ztermy::ai::AiProviderError;
using ztermy::ai::AiStreamEventType;
using ztermy::ai::AiTokenUsage;
using ztermy::ai::OllamaStreamMapper;
using ztermy::ai::OpenAiCompatibleStreamMapper;
using ztermy::ai::OpenAiResponsesStreamMapper;
using ztermy::ai::ServerSentEvent;

class ProviderStreamMapperTests final : public QObject
{
    Q_OBJECT

private slots:
    void mapsOpenAiTextAndUsage();
    void mapsOpenAiToolArguments();
    void rejectsMalformedProviderJson();
    void mapsCompatibleTextToolsAndCompletion();
    void mapsOllamaThinkingToolsAndUsage();
    void mapsOllamaError();
};

void ProviderStreamMapperTests::mapsOpenAiTextAndUsage()
{
    OpenAiResponsesStreamMapper mapper;
    auto events = mapper.map(ServerSentEvent{.data =
                                                 R"json({"type":"response.created","response":{"id":"resp_1"}})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->front().type, AiStreamEventType::responseStarted);
    QCOMPARE(events->front().responseId, std::string("resp_1"));

    events = mapper.map(ServerSentEvent{.data = R"json({"type":"response.output_text.delta","item_id":"msg_1","delta":"你好"})json"});
    QCOMPARE(events->front().type, AiStreamEventType::textDelta);
    QCOMPARE(events->front().delta, std::string("你好"));

    events = mapper.map(ServerSentEvent{.data = R"json({"type":"response.completed","response":{"id":"resp_1","usage":{"input_tokens":12,"output_tokens":7,"input_tokens_details":{"cached_tokens":4},"output_tokens_details":{"reasoning_tokens":2}}}})json"});
    QCOMPARE(events->size(), std::size_t{2});
    QCOMPARE(events->at(0).type, AiStreamEventType::usageUpdated);
    QVERIFY(events->at(0).usage.has_value());
    const auto usage = events->at(0).usage.value_or(AiTokenUsage{});
    QCOMPARE(usage.inputTokens, std::uint64_t{12});
    QCOMPARE(usage.reasoningTokens, std::uint64_t{2});
    QCOMPARE(events->at(1).type, AiStreamEventType::responseCompleted);
}

void ProviderStreamMapperTests::mapsOpenAiToolArguments()
{
    OpenAiResponsesStreamMapper mapper;
    auto events = mapper.map(ServerSentEvent{.data = R"json({"type":"response.output_item.added","item":{"type":"function_call","id":"item_1","call_id":"call_1","name":"run_command"}})json"});
    QCOMPARE(events->front().type, AiStreamEventType::toolCallStarted);
    QCOMPARE(events->front().toolCallId, std::string("call_1"));

    events = mapper.map(ServerSentEvent{.data = R"json({"type":"response.function_call_arguments.delta","item_id":"item_1","delta":"{\"command\":"})json"});
    QCOMPARE(events->front().type, AiStreamEventType::toolArgumentsDelta);
    QCOMPARE(events->front().toolName, std::string("run_command"));

    events = mapper.map(ServerSentEvent{.data = R"json({"type":"response.function_call_arguments.done","item_id":"item_1","arguments":"{\"command\":\"pwd\"}"})json"});
    QCOMPARE(events->front().type, AiStreamEventType::toolCallCompleted);
    QCOMPARE(events->front().delta, std::string(R"json({"command":"pwd"})json"));
}

void ProviderStreamMapperTests::rejectsMalformedProviderJson()
{
    OpenAiResponsesStreamMapper mapper;
    const auto result = mapper.map(ServerSentEvent{.data = "not-json"});
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, AiProviderErrorCode::protocol);
}

void ProviderStreamMapperTests::mapsCompatibleTextToolsAndCompletion()
{
    OpenAiCompatibleStreamMapper mapper;
    auto events = mapper.map(ServerSentEvent{.data = R"json({"id":"chat_1","choices":[{"delta":{"content":"hi","tool_calls":[{"index":0,"id":"call_1","function":{"name":"run_command","arguments":"{\""}}]},"finish_reason":null}]})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->at(1).type, AiStreamEventType::textDelta);
    QCOMPARE(events->at(2).type, AiStreamEventType::toolCallStarted);
    QCOMPARE(events->at(3).type, AiStreamEventType::toolArgumentsDelta);

    events = mapper.map(ServerSentEvent{.data = "[DONE]"});
    QCOMPARE(events->size(), std::size_t{2});
    QCOMPARE(events->at(0).type, AiStreamEventType::toolCallCompleted);
    QCOMPARE(events->at(1).type, AiStreamEventType::responseCompleted);
    QVERIFY(mapper.map(ServerSentEvent{.data = "[DONE]"})->empty());
}

void ProviderStreamMapperTests::mapsOllamaThinkingToolsAndUsage()
{
    OllamaStreamMapper mapper;
    const auto events = mapper.map(R"json({"message":{"content":"answer","thinking":"plan","tool_calls":[{"function":{"name":"run_command","arguments":{"command":"pwd"}}}]},"done":true,"prompt_eval_count":9,"eval_count":4})json");
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), std::size_t{7});
    QCOMPARE(events->at(0).type, AiStreamEventType::responseStarted);
    QCOMPARE(events->at(1).type, AiStreamEventType::textDelta);
    QCOMPARE(events->at(2).type, AiStreamEventType::reasoningDelta);
    QCOMPARE(events->at(3).type, AiStreamEventType::toolCallStarted);
    QCOMPARE(events->at(4).type, AiStreamEventType::toolCallCompleted);
    QVERIFY(events->at(5).usage.has_value());
    const auto usage = events->at(5).usage.value_or(AiTokenUsage{});
    QCOMPARE(usage.outputTokens, std::uint64_t{4});
    QCOMPARE(events->at(6).type, AiStreamEventType::responseCompleted);
}

void ProviderStreamMapperTests::mapsOllamaError()
{
    OllamaStreamMapper mapper;
    const auto events = mapper.map(R"json({"error":"model not found"})json");
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), std::size_t{1});
    QCOMPARE(events->front().type, AiStreamEventType::responseFailed);
    QVERIFY(events->front().error.has_value());
    const auto error = events->front().error.value_or(AiProviderError{});
    QCOMPARE(error.message, std::string("model not found"));
}

} // namespace

QTEST_GUILESS_MAIN(ProviderStreamMapperTests)

#include "provider_stream_mapper_tests.moc"
