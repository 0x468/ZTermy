#include "domain/ai/NdjsonParser.h"
#include "domain/ai/ServerSentEventParser.h"

#include <QTest>

#include <cstddef>
#include <string>

namespace
{

using ztermy::ai::NdjsonParser;
using ztermy::ai::NdjsonParserLimits;
using ztermy::ai::ServerSentEventParser;
using ztermy::ai::ServerSentEventParserLimits;
using ztermy::ai::StreamParseErrorCode;

class AiStreamParserTests final : public QObject
{
    Q_OBJECT

private slots:
    void parsesSplitServerSentEvents();
    void preservesMultilineDataAndMetadata();
    void dispatchesPendingEventAtEof();
    void ignoresCommentsAndInvalidRetry();
    void boundsServerSentEventInput();
    void parsesSplitNdjsonLines();
    void finishesNonTerminatedNdjsonLine();
    void boundsNdjsonInput();
};

void AiStreamParserTests::parsesSplitServerSentEvents()
{
    ServerSentEventParser parser;
    auto events = parser.append(R"json(data: {"type":"response.output_)json");
    QVERIFY(events.has_value());
    QVERIFY(events->empty());

    events = parser.append("text.delta\",\"delta\":\"你\"}\r\n\r\ndata: second\n\n");
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), std::size_t{2});
    QCOMPARE(events->at(0).data, std::string(R"json({"type":"response.output_text.delta","delta":"你"})json"));
    QCOMPARE(events->at(1).data, std::string("second"));
}

void AiStreamParserTests::preservesMultilineDataAndMetadata()
{
    ServerSentEventParser parser;
    const auto events = parser.append("id: turn-7\nevent: response\nretry: 2500\ndata: first\ndata: second\n\n");
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), std::size_t{1});
    QCOMPARE(events->front().event, std::string("response"));
    QCOMPARE(events->front().id, std::string("turn-7"));
    QCOMPARE(events->front().retryMilliseconds, std::optional<std::uint64_t>{2500});
    QCOMPARE(events->front().data, std::string("first\nsecond"));

    const auto next = parser.append("data: next\n\n");
    QCOMPARE(next->front().id, std::string("turn-7"));
    QVERIFY(!next->front().retryMilliseconds.has_value());
}

void AiStreamParserTests::dispatchesPendingEventAtEof()
{
    ServerSentEventParser parser;
    QVERIFY(parser.append("data: {\"done\":true}").value().empty());
    const auto events = parser.finish();
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), std::size_t{1});
    QCOMPARE(events->front().data, std::string("{\"done\":true}"));
}

void AiStreamParserTests::ignoresCommentsAndInvalidRetry()
{
    ServerSentEventParser parser;
    const auto events = parser.append(": keepalive\nretry: later\nunknown: ignored\ndata:\n\n");
    QVERIFY(events.has_value());
    QCOMPARE(events->size(), std::size_t{1});
    QCOMPARE(events->front().data, std::string{});
    QVERIFY(!events->front().retryMilliseconds.has_value());
}

void AiStreamParserTests::boundsServerSentEventInput()
{
    ServerSentEventParser parser(
        ServerSentEventParserLimits{.maxLineBytes = 12, .maxEventBytes = 8, .maxBufferedBytes = 32});
    const auto result = parser.append("data: 123456789\n");
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, StreamParseErrorCode::lineTooLarge);
    QVERIFY(!parser.append("data: ok\n\n").has_value());

    parser.reset();
    const auto eventResult = parser.append("data: 1234\ndata: 5678\n");
    QVERIFY(!eventResult.has_value());
    QCOMPARE(eventResult.error().code, StreamParseErrorCode::eventTooLarge);
}

void AiStreamParserTests::parsesSplitNdjsonLines()
{
    NdjsonParser parser;
    QVERIFY(parser.append("{\"message\":\"你").value().empty());
    const auto lines = parser.append("好\"}\r\n\n{\"done\":true}\n");
    QVERIFY(lines.has_value());
    QCOMPARE(lines->size(), std::size_t{2});
    QCOMPARE(lines->at(0), std::string("{\"message\":\"你好\"}"));
    QCOMPARE(lines->at(1), std::string("{\"done\":true}"));
}

void AiStreamParserTests::finishesNonTerminatedNdjsonLine()
{
    NdjsonParser parser;
    QVERIFY(parser.append("{\"done\":true}").value().empty());
    const auto lines = parser.finish();
    QCOMPARE(lines->size(), std::size_t{1});
    QCOMPARE(lines->front(), std::string("{\"done\":true}"));
}

void AiStreamParserTests::boundsNdjsonInput()
{
    NdjsonParser parser(NdjsonParserLimits{.maxLineBytes = 8, .maxBufferedBytes = 16});
    const auto result = parser.append("123456789");
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, StreamParseErrorCode::lineTooLarge);
}

} // namespace

QTEST_GUILESS_MAIN(AiStreamParserTests)

#include "ai_stream_parser_tests.moc"
