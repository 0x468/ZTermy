#include "application/ai/AiTerminalOutputTool.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

#include <string>

namespace
{

using ztermy::ai::AiSessionTarget;
using ztermy::ai::AiTerminalOutputTool;
using ztermy::terminal::TerminalScrollbackAnchor;
using ztermy::terminal::TerminalScrollbackPage;

[[nodiscard]] QJsonObject object(const std::string &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(value)).object();
}

[[nodiscard]] AiSessionTarget target()
{
    return {.sessionId = "current-terminal", .sessionGeneration = 7};
}

class AiTerminalOutputToolTests final : public QObject
{
    Q_OBJECT

private slots:
    void publishesCurrentTerminalTailContract();
    void rejectsLooseOrOutOfRangeArguments();
    void readsHeadAndTailPagesWithoutCollapsingBlankLines();
    void boundsTailPagesAndPreservesUtf8();
};

void AiTerminalOutputToolTests::publishesCurrentTerminalTailContract()
{
    const auto definition = AiTerminalOutputTool::definition();
    QCOMPARE(definition.name, std::string("read_terminal_output"));
    const QJsonObject schema = object(definition.parametersJson);
    QVERIFY(!schema.value(QStringLiteral("additionalProperties")).toBool(true));
    QVERIFY(definition.parametersJson.find("session_id") == std::string::npos);

    const auto parsed =
        AiTerminalOutputTool::parse(R"({"anchor":"tail","offset":0,"line_count":80,"max_bytes":16384})", target());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->target.sessionId, std::string("current-terminal"));
    QCOMPARE(parsed->target.sessionGeneration, std::uint64_t{7});
    QCOMPARE(parsed->anchor, TerminalScrollbackAnchor::tail);
    QCOMPARE(parsed->offset, std::size_t{0});
    QCOMPARE(parsed->lineCount, std::size_t{80});
}

void AiTerminalOutputToolTests::rejectsLooseOrOutOfRangeArguments()
{
    QVERIFY(!AiTerminalOutputTool::parse(R"({"anchor":"tail","offset":0,"line_count":301,"max_bytes":16384})", target())
                 .has_value());
    QVERIFY(!AiTerminalOutputTool::parse(R"({"anchor":"middle","offset":0,"line_count":1,"max_bytes":256})", target())
                 .has_value());
    QVERIFY(!AiTerminalOutputTool::parse(R"({"anchor":"head","offset":0,"line_count":1,"max_bytes":255})", target())
                 .has_value());
    QVERIFY(!AiTerminalOutputTool::parse(
                 R"({"anchor":"head","offset":0,"line_count":1,"max_bytes":256,"session_id":"other"})", target())
                 .has_value());
}

void AiTerminalOutputToolTests::readsHeadAndTailPagesWithoutCollapsingBlankLines()
{
    const TerminalScrollbackPage page{.lines = {"old", "", "new"},
                                      .firstLine = 10,
                                      .totalLines = 20,
                                      .scrollbackLines = 17};
    auto headRequest =
        AiTerminalOutputTool::parse(R"({"anchor":"head","offset":10,"line_count":3,"max_bytes":256})", target());
    QVERIFY(headRequest.has_value());
    auto head = AiTerminalOutputTool::read(*headRequest, page);
    QCOMPARE(head.content, std::string("old\n\nnew"));
    QCOMPARE(head.firstLine, std::size_t{10});
    QCOMPARE(head.lineCount, std::size_t{3});
    QCOMPARE(head.nextOffset, std::size_t{13});
    QVERIFY(head.hasMore);
    QVERIFY(!head.truncated);

    auto tailRequest =
        AiTerminalOutputTool::parse(R"({"anchor":"tail","offset":0,"line_count":3,"max_bytes":256})", target());
    QVERIFY(tailRequest.has_value());
    auto tail =
        object(AiTerminalOutputTool::result(*tailRequest, page)).value(QStringLiteral("terminal_output")).toObject();
    QCOMPARE(tail.value(QStringLiteral("content")).toString(), QStringLiteral("old\n\nnew"));
    QCOMPARE(tail.value(QStringLiteral("first_line")).toInteger(), qint64{10});
    QCOMPARE(tail.value(QStringLiteral("next_offset")).toInteger(), qint64{3});
    QCOMPARE(tail.value(QStringLiteral("scrollback_lines")).toInteger(), qint64{17});
    QVERIFY(!tail.value(QStringLiteral("includes_active_screen")).toBool());
}

void AiTerminalOutputToolTests::boundsTailPagesAndPreservesUtf8()
{
    const TerminalScrollbackPage page{.lines = {std::string(200, 'a'), std::string(200, 'b'), std::string(200, 'c')},
                                      .firstLine = 40,
                                      .totalLines = 43,
                                      .scrollbackLines = 41};
    auto request =
        AiTerminalOutputTool::parse(R"({"anchor":"tail","offset":0,"line_count":3,"max_bytes":256})", target());
    QVERIFY(request.has_value());
    const auto bounded = AiTerminalOutputTool::read(*request, page);
    QCOMPARE(bounded.content, std::string(200, 'c'));
    QCOMPARE(bounded.firstLine, std::size_t{42});
    QCOMPARE(bounded.nextOffset, std::size_t{1});
    QVERIFY(bounded.hasMore);
    QVERIFY(bounded.truncated);
    QVERIFY(!bounded.partialLine);

    const TerminalScrollbackPage unicodePage{.lines = {std::string(100, '\0')},
                                             .firstLine = 0,
                                             .totalLines = 1,
                                             .scrollbackLines = 0};
    auto unicode = unicodePage;
    unicode.lines.front().clear();
    for (int index = 0; index < 100; ++index)
    {
        unicode.lines.front().append("你");
    }
    const auto partial = AiTerminalOutputTool::read(*request, unicode);
    QCOMPARE(partial.bytesRead, std::size_t{255});
    QCOMPARE(QString::fromUtf8(partial.content), QString(85, QChar(0x4F60)));
    QVERIFY(partial.partialLine);
    QVERIFY(partial.truncated);
}

} // namespace

QTEST_GUILESS_MAIN(AiTerminalOutputToolTests)

#include "ai_terminal_output_tool_tests.moc"
