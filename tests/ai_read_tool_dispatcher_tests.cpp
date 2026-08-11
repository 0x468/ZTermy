#include "application/ai/AiReadToolDispatcher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

#include <vector>

namespace
{

using ztermy::ai::AiReadToolDispatcher;
using ztermy::ai::AiTerminalReadSnapshot;
using ztermy::terminal::CommandBlock;
using ztermy::terminal::CommandBlockState;
using ztermy::terminal::TerminalSemanticCapability;

[[nodiscard]] QJsonObject object(const std::string &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(value)).object();
}

[[nodiscard]] std::vector<AiTerminalReadSnapshot> sessions()
{
    CommandBlock block{.id = 9,
                       .command = "false",
                       .workingDirectory = "/tmp",
                       .sessionId = "session-1",
                       .host = "host",
                       .shell = "bash",
                       .sessionGeneration = 4,
                       .capability = TerminalSemanticCapability::rich,
                       .state = CommandBlockState::finished,
                       .exitStatus = 1};
    block.retainedOutput = {static_cast<std::byte>('n'), static_cast<std::byte>('o')};
    block.retainedHeadBytes = 2;
    block.observedOutputBytes = 2;
    block.nextOutputStreamOffset = 2;
    block.retainedTailStreamOffset = 2;
    return {AiTerminalReadSnapshot{.sessionId = "session-1",
                                   .title = "Test",
                                   .host = "host",
                                   .shell = "bash",
                                   .workingDirectory = "/tmp",
                                   .terminalFrame = "one\ntwo\nthree\n",
                                   .sessionGeneration = 4,
                                   .capability = TerminalSemanticCapability::rich,
                                   .connected = true,
                                   .commandBlocks = {block}}};
}

class AiReadToolDispatcherTests final : public QObject
{
    Q_OBJECT

private slots:
    void publishesStrictReadOnlyCatalog();
    void executesBoundedReads();
    void rejectsMalformedStaleAndUnknownRequests();
};

void AiReadToolDispatcherTests::publishesStrictReadOnlyCatalog()
{
    const auto definitions = AiReadToolDispatcher::definitions();
    QCOMPARE(definitions.size(), std::size_t{5});
    QCOMPARE(definitions.front().name, std::string("list_sessions"));
    for (const auto &definition : definitions)
    {
        QVERIFY(QJsonDocument::fromJson(QByteArray::fromStdString(definition.parametersJson)).isObject());
        QVERIFY(!definition.name.contains("run"));
        QVERIFY(!definition.name.contains("write"));
    }
}

void AiReadToolDispatcherTests::executesBoundedReads()
{
    const AiReadToolDispatcher dispatcher;
    const auto snapshots = sessions();
    auto result = object(dispatcher.execute("list_sessions", "{}", snapshots));
    QVERIFY(result.value("ok").toBool());
    QCOMPARE(result.value("sessions").toArray().size(), 1);

    result = object(dispatcher.execute(
        "read_terminal", R"({"session_id":"session-1","session_generation":4,"first_line":1,"line_count":1})",
        snapshots));
    QVERIFY(result.value("ok").toBool());
    QCOMPARE(result.value("terminal").toObject().value("content").toString(), QStringLiteral("two\n"));
    QVERIFY(result.value("terminal").toObject().value("untrusted_evidence").toBool());

    result = object(dispatcher.execute("read_command_block",
                                       R"({"session_id":"session-1","session_generation":4,"block_id":9})", snapshots));
    QVERIFY(result.value("ok").toBool());
    QCOMPARE(result.value("command_block").toObject().value("exit_status").toInt(), 1);
    QCOMPARE(result.value("command_block").toObject().value("output").toString(), QStringLiteral("no"));

    result = object(dispatcher.execute(
        "read_command_output",
        R"({"session_id":"session-1","session_generation":4,"block_id":9,"after_cursor":0,"max_bytes":1})", snapshots));
    QVERIFY(result.value("ok").toBool());
    const auto output = result.value("command_output").toObject();
    QCOMPARE(output.value("output").toString(), QStringLiteral("n"));
    QCOMPARE(output.value("next_cursor").toInt(), 1);
    QVERIFY(output.value("has_more").toBool());
}

void AiReadToolDispatcherTests::rejectsMalformedStaleAndUnknownRequests()
{
    const AiReadToolDispatcher dispatcher;
    const auto snapshots = sessions();
    auto result = object(dispatcher.execute("read_terminal", "[]", snapshots));
    QVERIFY(!result.value("ok").toBool());
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("invalid_arguments"));

    result = object(
        dispatcher.execute("read_session_info", R"({"session_id":"session-1","session_generation":3})", snapshots));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("scope_changed"));

    result = object(dispatcher.execute(
        "read_session_info", R"({"session_id":"session-1","session_generation":4,"unexpected":true})", snapshots));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("invalid_arguments"));

    result = object(dispatcher.execute("list_sessions", R"({"unexpected":true})", snapshots));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("invalid_arguments"));

    result = object(dispatcher.execute("run_command", "{}", snapshots));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("unsupported"));
}

} // namespace

QTEST_GUILESS_MAIN(AiReadToolDispatcherTests)

#include "ai_read_tool_dispatcher_tests.moc"
