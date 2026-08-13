#include "application/ai/AiReadToolDispatcher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

#include <array>
#include <string_view>
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
    ztermy::ai::AiOperationsReadSnapshot operations;
    operations.sftpState = "ready";
    operations.sftpPath = "/home/test";
    operations.sftpHomePath = "/home/test";
    operations.sftpListingAvailable = true;
    operations.sftpEntries = {{.name = "file.txt", .remotePath = "/home/test/file.txt", .type = "file", .size = 12}};
    operations.shellHistory = {{.command = "pwd", .shell = "bash", .timestampUtcSeconds = 123}};
    operations.scripts = {
        {.id = "script-1",
         .name = "Inspect",
         .description = "Inspect host",
         .shell = "bash",
         .variableCount = 1,
         .stepCount = 2,
         .modifiedUtcMs = 456,
         .variables =
             {{.name = "path", .label = "Path", .type = "text", .choices = {"/tmp", "/var/tmp"}, .required = true}},
         .steps = {{.command = "OPENAI_API_KEY=sk-abcdefghijklmnopqrstuvwxyz123456 ls {{path}}",
                    .continuation = "immediate",
                    .timeoutMs = 30000}}}};
    operations.notes = {{.path = "ops.md", .name = "ops.md", .size = 24, .modifiedUtcMs = 789}};
    operations.portForwarding = {{.id = "forward-1",
                                  .label = "Web",
                                  .profileName = "Test",
                                  .type = "local",
                                  .bindHost = "127.0.0.1",
                                  .bindPort = 8080,
                                  .destinationHost = "127.0.0.1",
                                  .destinationPort = 80,
                                  .state = "running"}};
    operations.telemetry = {.state = "ready",
                            .osName = "Linux",
                            .cpuPercent = 12.5,
                            .cpuCoreCount = 4,
                            .memoryUsedKiB = 1024,
                            .memoryTotalKiB = 4096,
                            .receivedBytesPerSecond = 10,
                            .transmittedBytesPerSecond = 20,
                            .sshProbeLatencyMs = 3};
    return {AiTerminalReadSnapshot{.sessionId = "session-1",
                                   .title = "Test",
                                   .host = "host",
                                   .shell = "bash",
                                   .workingDirectory = "/tmp",
                                   .terminalFrame = "one\ntwo\nthree\n",
                                   .sessionGeneration = 4,
                                   .capability = TerminalSemanticCapability::rich,
                                   .connected = true,
                                   .commandBlocks = {block},
                                   .operations = std::move(operations)}};
}

class AiReadToolDispatcherTests final : public QObject
{
    Q_OBJECT

private slots:
    void publishesStrictReadOnlyCatalog();
    void executesBoundedReads();
    void rejectsMalformedStaleAndUnknownRequests();
    void rejectsOversizedOperationsResults();
};

void AiReadToolDispatcherTests::publishesStrictReadOnlyCatalog()
{
    const auto definitions = AiReadToolDispatcher::definitions();
    constexpr std::array expected{
        std::string_view{"list_sessions"},        std::string_view{"read_multi_session_status"},
        std::string_view{"read_session_info"},    std::string_view{"read_terminal"},
        std::string_view{"read_command_block"},   std::string_view{"read_command_output"},
        std::string_view{"read_terminal_output"}, std::string_view{"list_sftp_directory"},
        std::string_view{"list_shell_history"},   std::string_view{"list_scripts"},
        std::string_view{"read_script"},          std::string_view{"list_notes"},
        std::string_view{"read_remote_telemetry"},
        std::string_view{"list_port_forwarding"},
    };
    QCOMPARE(definitions.size(), expected.size());
    for (std::size_t index = 0; index < definitions.size(); ++index)
    {
        const auto &definition = definitions[index];
        QCOMPARE(definition.name, expected[index]);
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
        "read_multi_session_status",
        R"({"targets":[{"session_id":"session-1","session_generation":4},{"session_id":"missing","session_generation":1}]})",
        snapshots));
    const auto multiSession = result.value("results").toObject().value("items").toArray();
    QCOMPARE(multiSession.size(), 2);
    QVERIFY(multiSession.at(0).toObject().value("ok").toBool());
    QCOMPARE(multiSession.at(0).toObject().value("telemetry").toObject().value("cpu_percent").toDouble(), 12.5);
    QVERIFY(!multiSession.at(1).toObject().value("ok").toBool());

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

    result = object(dispatcher.execute("list_sftp_directory",
                                       R"({"session_id":"session-1","session_generation":4,"offset":0,"limit":10})",
                                       snapshots));
    QVERIFY(result.value("ok").toBool());
    const auto directory = result.value("sftp_directory").toObject();
    QCOMPARE(directory.value("path").toString(), QStringLiteral("/home/test"));
    QCOMPARE(directory.value("items").toArray().at(0).toObject().value("name").toString(), QStringLiteral("file.txt"));

    result = object(dispatcher.execute(
        "list_shell_history", R"({"session_id":"session-1","session_generation":4,"offset":0,"limit":10})", snapshots));
    QCOMPARE(
        result.value("shell_history").toObject().value("items").toArray().at(0).toObject().value("command").toString(),
        QStringLiteral("pwd"));

    result = object(dispatcher.execute(
        "list_scripts", R"({"session_id":"session-1","session_generation":4,"offset":0,"limit":10})", snapshots));
    const auto script = result.value("scripts").toObject().value("items").toArray().at(0).toObject();
    QCOMPARE(script.value("name").toString(), QStringLiteral("Inspect"));
    QVERIFY(!script.contains("command"));

    result = object(dispatcher.execute(
        "read_script", R"({"session_id":"session-1","session_generation":4,"script_id":"script-1"})", snapshots));
    const auto scriptContent = result.value("script").toObject();
    QVERIFY(result.value("ok").toBool());
    const QString command = scriptContent.value("steps").toArray().at(0).toObject().value("command").toString();
    QVERIFY(!command.contains(QStringLiteral("sk-abcdefghijklmnopqrstuvwxyz")));
    QVERIFY(scriptContent.value("redacted").toBool());
    const auto variable = scriptContent.value("variables").toArray().at(0).toObject();
    QCOMPARE(variable.value("name").toString(), QStringLiteral("path"));
    QVERIFY(!variable.contains("default_value"));
    QVERIFY(scriptContent.value("untrusted_evidence").toBool());

    result = object(dispatcher.execute(
        "list_notes", R"({"session_id":"session-1","session_generation":4,"offset":0,"limit":10})", snapshots));
    QCOMPARE(result.value("notes").toObject().value("items").toArray().at(0).toObject().value("path").toString(),
             QStringLiteral("ops.md"));

    result = object(
        dispatcher.execute("read_remote_telemetry", R"({"session_id":"session-1","session_generation":4})", snapshots));
    QCOMPARE(result.value("telemetry").toObject().value("cpu_percent").toDouble(), 12.5);

    result = object(dispatcher.execute("list_port_forwarding",
                                       R"({"session_id":"session-1","session_generation":4,"offset":0,"limit":10})",
                                       snapshots));
    QCOMPARE(
        result.value("port_forwarding").toObject().value("items").toArray().at(0).toObject().value("state").toString(),
        QStringLiteral("running"));
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

    result = object(dispatcher.execute(
        "read_multi_session_status",
        R"({"targets":[{"session_id":"session-1","session_generation":4},{"session_id":"session-1","session_generation":4}]})",
        snapshots));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("invalid_arguments"));

    result = object(dispatcher.execute("run_command", "{}", snapshots));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("unsupported"));

    result = object(dispatcher.execute(
        "list_scripts", R"({"session_id":"session-1","session_generation":4,"offset":0,"limit":101})", snapshots));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("invalid_arguments"));

    result = object(dispatcher.execute(
        "list_notes", R"({"session_id":"session-1","session_generation":3,"offset":0,"limit":10})", snapshots));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("scope_changed"));

    result = object(dispatcher.execute(
        "read_script", R"({"session_id":"session-1","session_generation":4,"script_id":"missing"})", snapshots));
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("not_found"));
}

void AiReadToolDispatcherTests::rejectsOversizedOperationsResults()
{
    const AiReadToolDispatcher dispatcher;
    auto snapshots = sessions();
    snapshots.front().operations.shellHistory.clear();
    for (int index = 0; index < 100; ++index)
    {
        snapshots.front().operations.shellHistory.push_back(
            {.command = std::string(1024, 'x'), .shell = "bash", .timestampUtcSeconds = index});
    }
    const auto result = object(
        dispatcher.execute("list_shell_history",
                           R"({"session_id":"session-1","session_generation":4,"offset":0,"limit":100})", snapshots));
    QVERIFY(!result.value("ok").toBool());
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("limit_exceeded"));
}

} // namespace

QTEST_GUILESS_MAIN(AiReadToolDispatcherTests)

#include "ai_read_tool_dispatcher_tests.moc"
