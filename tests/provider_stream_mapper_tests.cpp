#include "infrastructure/ai/ProviderStreamMapper.h"

#include <QTest>

#include <string>

namespace
{

using ztermy::ai::AiProviderError;
using ztermy::ai::AiProviderErrorCode;
using ztermy::ai::AiStreamEventType;
using ztermy::ai::AiTokenUsage;
using ztermy::ai::AnthropicStreamMapper;
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
    void mapsOpenAiWebSearchAndCitations();
    void rejectsMalformedProviderJson();
    void mapsCompatibleTextToolsAndCompletion();
    void mapsAnthropicTextToolsUsageAndCompletion();
    void mapsAnthropicThinkingSignature();
    void mapsAnthropicWebSearchAndCitations();
    void mapsOllamaThinkingToolsAndUsage();
    void mapsOllamaError();
};

void ProviderStreamMapperTests::mapsOpenAiTextAndUsage()
{
    OpenAiResponsesStreamMapper mapper;
    auto events =
        mapper.map(ServerSentEvent{.data = R"json({"type":"response.created","response":{"id":"resp_1"}})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->front().type, AiStreamEventType::responseStarted);
    QCOMPARE(events->front().responseId, std::string("resp_1"));

    events = mapper.map(
        ServerSentEvent{.data = R"json({"type":"response.output_text.delta","item_id":"msg_1","delta":"你好"})json"});
    QCOMPARE(events->front().type, AiStreamEventType::textDelta);
    QCOMPARE(events->front().delta, std::string("你好"));

    events = mapper.map(ServerSentEvent{
        .data =
            R"json({"type":"response.completed","response":{"id":"resp_1","usage":{"input_tokens":12,"output_tokens":7,"input_tokens_details":{"cached_tokens":4},"output_tokens_details":{"reasoning_tokens":2}}}})json"});
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
    auto events = mapper.map(ServerSentEvent{
        .data =
            R"json({"type":"response.output_item.added","item":{"type":"function_call","id":"item_1","call_id":"call_1","name":"run_command"}})json"});
    QCOMPARE(events->front().type, AiStreamEventType::toolCallStarted);
    QCOMPARE(events->front().toolCallId, std::string("call_1"));

    events = mapper.map(ServerSentEvent{
        .data =
            R"json({"type":"response.function_call_arguments.delta","item_id":"item_1","delta":"{\"command\":"})json"});
    QCOMPARE(events->front().type, AiStreamEventType::toolArgumentsDelta);
    QCOMPARE(events->front().toolName, std::string("run_command"));

    events = mapper.map(ServerSentEvent{
        .data =
            R"json({"type":"response.function_call_arguments.done","item_id":"item_1","arguments":"{\"command\":\"pwd\"}"})json"});
    QCOMPARE(events->front().type, AiStreamEventType::toolCallCompleted);
    QCOMPARE(events->front().delta, std::string(R"json({"command":"pwd"})json"));
}

void ProviderStreamMapperTests::mapsOpenAiWebSearchAndCitations()
{
    OpenAiResponsesStreamMapper mapper;
    auto events = mapper.map(ServerSentEvent{
        .data =
            R"json({"type":"response.output_item.added","item":{"type":"web_search_call","id":"ws_1","action":{"query":"Qt 6.8 release notes"}}})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), std::size_t{2});
    QCOMPARE(events->at(0).type, AiStreamEventType::webSearchStarted);
    QCOMPARE(events->at(1).type, AiStreamEventType::webSearchQuery);
    QCOMPARE(events->at(1).delta, std::string("Qt 6.8 release notes"));

    events = mapper.map(ServerSentEvent{
        .data =
            R"json({"type":"response.output_text.annotation.added","item_id":"msg_1","annotation":{"type":"url_citation","url":"https://doc.qt.io/qt-6/whatsnew68.html","title":"What's New in Qt 6.8"}})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->front().type, AiStreamEventType::webSourceAdded);
    QVERIFY(events->front().webSource.has_value());
    QCOMPARE(events->front().webSource->url, std::string("https://doc.qt.io/qt-6/whatsnew68.html"));
    QCOMPARE(events->front().webSource->title, std::string("What's New in Qt 6.8"));

    events = mapper.map(
        ServerSentEvent{.data = R"json({"type":"response.web_search_call.completed","item_id":"ws_1"})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->front().type, AiStreamEventType::webSearchCompleted);
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
    auto events = mapper.map(ServerSentEvent{
        .data =
            R"json({"id":"chat_1","choices":[{"delta":{"content":"hi","tool_calls":[{"index":0,"id":"call_1","function":{"name":"run_command","arguments":"{\""}}]},"finish_reason":null}]})json"});
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

void ProviderStreamMapperTests::mapsAnthropicTextToolsUsageAndCompletion()
{
    AnthropicStreamMapper mapper;
    auto events = mapper.map(ServerSentEvent{
        .event = "message_start",
        .data =
            R"json({"type":"message_start","message":{"id":"msg_1","usage":{"input_tokens":9,"cache_read_input_tokens":3}}})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->at(0).type, AiStreamEventType::responseStarted);
    QCOMPARE(events->at(1).usage.value_or(AiTokenUsage{}).inputTokens, std::uint64_t{9});

    events = mapper.map(ServerSentEvent{
        .event = "content_block_start",
        .data =
            R"json({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"tool_1","name":"run_command"}})json"});
    QCOMPARE(events->front().type, AiStreamEventType::toolCallStarted);
    events = mapper.map(ServerSentEvent{
        .event = "content_block_delta",
        .data =
            R"json({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"command\":"}})json"});
    QCOMPARE(events->front().type, AiStreamEventType::toolArgumentsDelta);
    events = mapper.map(ServerSentEvent{
        .event = "content_block_delta",
        .data = R"json({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"hello"}})json"});
    QCOMPARE(events->front().delta, std::string("hello"));
    events = mapper.map(
        ServerSentEvent{.event = "content_block_stop", .data = R"json({"type":"content_block_stop","index":1})json"});
    QCOMPARE(events->front().type, AiStreamEventType::toolCallCompleted);
    events = mapper.map(ServerSentEvent{.event = "message_delta",
                                        .data = R"json({"type":"message_delta","usage":{"output_tokens":4}})json"});
    QCOMPARE(events->front().usage.value_or(AiTokenUsage{}).outputTokens, std::uint64_t{4});
    events = mapper.map(ServerSentEvent{.event = "message_stop", .data = R"json({"type":"message_stop"})json"});
    QCOMPARE(events->front().type, AiStreamEventType::responseCompleted);
}

void ProviderStreamMapperTests::mapsAnthropicThinkingSignature()
{
    AnthropicStreamMapper mapper;
    const auto events = mapper.map(ServerSentEvent{
        .event = "content_block_delta",
        .data =
            R"json({"type":"content_block_delta","index":0,"delta":{"type":"signature_delta","signature":"signed"}})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), std::size_t{1});
    QCOMPARE(events->front().type, AiStreamEventType::reasoningSignatureDelta);
    QCOMPARE(events->front().delta, std::string("signed"));
}

void ProviderStreamMapperTests::mapsAnthropicWebSearchAndCitations()
{
    AnthropicStreamMapper mapper;
    auto events = mapper.map(ServerSentEvent{
        .event = "content_block_start",
        .data =
            R"json({"type":"content_block_start","index":1,"content_block":{"type":"server_tool_use","id":"srvtoolu_1","name":"web_search"}})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->front().type, AiStreamEventType::webSearchStarted);

    events = mapper.map(ServerSentEvent{
        .event = "content_block_delta",
        .data =
            R"json({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"query\":\"Qt 6.8 docs\"}"}})json"});
    QVERIFY(events.has_value());
    QVERIFY(events->empty());
    events = mapper.map(
        ServerSentEvent{.event = "content_block_stop", .data = R"json({"type":"content_block_stop","index":1})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->front().type, AiStreamEventType::webSearchQuery);
    QCOMPARE(events->front().delta, std::string("Qt 6.8 docs"));

    events = mapper.map(ServerSentEvent{
        .event = "content_block_start",
        .data =
            R"json({"type":"content_block_start","index":2,"content_block":{"type":"web_search_tool_result","tool_use_id":"srvtoolu_1","content":[{"type":"web_search_result","url":"https://doc.qt.io/qt-6/whatsnew68.html","title":"What's New in Qt 6.8"}]}})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), std::size_t{2});
    QCOMPARE(events->at(0).type, AiStreamEventType::webSourceAdded);
    QCOMPARE(events->at(1).type, AiStreamEventType::webSearchCompleted);

    events = mapper.map(ServerSentEvent{
        .event = "content_block_delta",
        .data =
            R"json({"type":"content_block_delta","index":3,"delta":{"type":"citations_delta","citation":{"type":"web_search_result_location","url":"https://doc.qt.io/qt-6/whatsnew68.html","title":"What's New in Qt 6.8","cited_text":"Qt 6.8 introduces updates."}}})json"});
    QVERIFY(events.has_value());
    QCOMPARE(events->front().type, AiStreamEventType::webSourceAdded);
    QCOMPARE(events->front().webSource->citedText, std::string("Qt 6.8 introduces updates."));
}

void ProviderStreamMapperTests::mapsOllamaThinkingToolsAndUsage()
{
    OllamaStreamMapper mapper;
    const auto events = mapper.map(
        R"json({"message":{"content":"answer","thinking":"plan","tool_calls":[{"function":{"name":"run_command","arguments":{"command":"pwd"}}}]},"done":true,"prompt_eval_count":9,"eval_count":4})json");
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
