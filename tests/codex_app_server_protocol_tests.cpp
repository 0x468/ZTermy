#include "infrastructure/ai/CodexAppServerProtocol.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

#include <optional>
#include <string>

namespace
{

[[nodiscard]] QJsonObject object(const QByteArray &framed)
{
    return QJsonDocument::fromJson(framed.trimmed()).object();
}

class CodexAppServerProtocolTests final : public QObject
{
    Q_OBJECT

private slots:
    void buildsHandshakeThreadTurnAndInterruptRequests();
    void buildsDynamicToolResponsesAndRejectsInvalidDefinitions();
    void parsesFragmentedResponsesNotificationsAndRequests();
    void rejectsMalformedAndOversizedInputThenResets();
};

void CodexAppServerProtocolTests::buildsHandshakeThreadTurnAndInterruptRequests()
{
    ztermy::ai::CodexAppServerProtocol protocol;
    const QJsonObject initialize = object(protocol.initializeRequest(1, "0.3.0"));
    QCOMPARE(initialize.value(QStringLiteral("method")).toString(), QStringLiteral("initialize"));
    QVERIFY(initialize.value(QStringLiteral("params"))
                .toObject()
                .value(QStringLiteral("capabilities"))
                .toObject()
                .value(QStringLiteral("experimentalApi"))
                .toBool());
    QCOMPARE(object(protocol.initializedNotification()).value(QStringLiteral("method")).toString(),
             QStringLiteral("initialized"));

    const std::vector tools{ztermy::ai::AiToolDefinition{
        .name = "run_command",
        .description = "Run a command in the owning ztermy terminal.",
        .parametersJson = R"({"type":"object","properties":{"command":{"type":"string"}},"required":["command"]})"}};
    const std::string workingDirectory = QDir::tempPath().toStdString();
    const auto thread = protocol.startThreadRequest(2, "gpt-5.6-terra", workingDirectory,
                                                    "Use only the owning ztermy terminal tools.", tools);
    QVERIFY(thread.has_value());
    const QJsonObject threadParams = object(*thread).value(QStringLiteral("params")).toObject();
    QCOMPARE(threadParams.value(QStringLiteral("sandbox")).toString(), QStringLiteral("read-only"));
    QCOMPARE(threadParams.value(QStringLiteral("approvalPolicy")).toString(), QStringLiteral("never"));
    const QJsonObject tool = threadParams.value(QStringLiteral("dynamicTools")).toArray().at(0).toObject();
    QCOMPARE(tool.value(QStringLiteral("name")).toString(), QStringLiteral("run_command"));
    QVERIFY(tool.value(QStringLiteral("inputSchema")).isObject());

    const auto turn = protocol.startTurnRequest(3, "thread-1", "Inspect disk usage");
    QVERIFY(turn.has_value());
    QCOMPARE(object(*turn).value(QStringLiteral("method")).toString(), QStringLiteral("turn/start"));
    const auto interrupt = protocol.interruptTurnRequest(4, "thread-1", "turn-1");
    QVERIFY(interrupt.has_value());
    QCOMPARE(object(*interrupt).value(QStringLiteral("method")).toString(), QStringLiteral("turn/interrupt"));
    const auto resume = protocol.resumeThreadRequest(5, "thread-1", "gpt-5.6-terra", workingDirectory,
                                                     "Use only the owning ztermy terminal tools.", tools);
    QVERIFY(resume.has_value());
    const QJsonObject resumeParams = object(*resume).value(QStringLiteral("params")).toObject();
    QCOMPARE(resumeParams.value(QStringLiteral("threadId")).toString(), QStringLiteral("thread-1"));
    QCOMPARE(resumeParams.value(QStringLiteral("dynamicTools")).toArray().size(), 1);
}

void CodexAppServerProtocolTests::buildsDynamicToolResponsesAndRejectsInvalidDefinitions()
{
    ztermy::ai::CodexAppServerProtocol protocol;
    const auto response = protocol.dynamicToolResponse(8, true, R"({"ok":true})");
    QVERIFY(response.has_value());
    const QJsonObject result = object(*response).value(QStringLiteral("result")).toObject();
    QVERIFY(result.value(QStringLiteral("success")).toBool());
    QCOMPARE(result.value(QStringLiteral("contentItems"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("type"))
                 .toString(),
             QStringLiteral("inputText"));

    const std::vector badName{ztermy::ai::AiToolDefinition{.name = "bad tool",
                                                           .description = "bad",
                                                           .parametersJson = R"({"type":"object"})"}};
    const std::string workingDirectory = QDir::tempPath().toStdString();
    QVERIFY(!protocol.startThreadRequest(1, "model", workingDirectory, "instructions", badName).has_value());
    const std::vector badSchema{
        ztermy::ai::AiToolDefinition{.name = "read_terminal", .description = "read", .parametersJson = "not-json"}};
    QVERIFY(!protocol.startThreadRequest(1, "model", workingDirectory, "instructions", badSchema).has_value());
    QVERIFY(!protocol.startThreadRequest(1, "model", "relative/path", "instructions", {}).has_value());
    QVERIFY(!protocol.dynamicToolResponse(9, false, std::string(256 * 1024 + 1, 'x')).has_value());
}

void CodexAppServerProtocolTests::parsesFragmentedResponsesNotificationsAndRequests()
{
    ztermy::ai::CodexAppServerProtocol protocol;
    const auto first = protocol.append(
        QByteArrayLiteral("{\"id\":1,\"result\":{\"thread\":{\"id\":\"thr-1\"}}}\n{\"method\":\"item/agent"));
    QVERIFY(first.has_value());
    QCOMPARE(first->size(), std::size_t{1});
    QCOMPARE(first->front().kind, ztermy::ai::CodexAppServerMessageKind::response);
    QVERIFY(first->front().id == std::optional<std::uint64_t>{1});

    const auto second = protocol.append(
        QByteArrayLiteral("Message/delta\",\"params\":{\"delta\":\"hello\"}}\n"
                          "{\"method\":\"item/tool/call\",\"id\":7,\"params\":{\"tool\":\"run_command\",\"arguments\":{"
                          "\"command\":\"pwd\"}}}\n"));
    QVERIFY(second.has_value());
    QCOMPARE(second->size(), std::size_t{2});
    QCOMPARE((*second)[0].kind, ztermy::ai::CodexAppServerMessageKind::notification);
    QCOMPARE((*second)[0].method, QStringLiteral("item/agentMessage/delta"));
    QCOMPARE((*second)[1].kind, ztermy::ai::CodexAppServerMessageKind::request);
    QVERIFY((*second)[1].id == std::optional<std::uint64_t>{7});
    QCOMPARE((*second)[1].params.value(QStringLiteral("tool")).toString(), QStringLiteral("run_command"));
}

void CodexAppServerProtocolTests::rejectsMalformedAndOversizedInputThenResets()
{
    ztermy::ai::CodexAppServerProtocol protocol;
    QVERIFY(!protocol.append(QByteArrayLiteral("not-json\n")).has_value());
    const auto afterReset = protocol.append(QByteArrayLiteral("{\"method\":\"turn/started\",\"params\":{}}\n"));
    QVERIFY(afterReset.has_value());
    QCOMPARE(afterReset->size(), std::size_t{1});

    QByteArray oversized(4 * 1024 * 1024 + 1, 'x');
    QVERIFY(!protocol.append(oversized).has_value());
    QByteArray unterminated(2 * 1024 * 1024 + 1, 'x');
    QVERIFY(!protocol.append(unterminated).has_value());
    const auto valid = protocol.append(QByteArrayLiteral("{\"id\":2,\"error\":{\"message\":\"failed\"}}\n"));
    QVERIFY(valid.has_value());
    QCOMPARE(valid->front().error.value(QStringLiteral("message")).toString(), QStringLiteral("failed"));
}

} // namespace

QTEST_GUILESS_MAIN(CodexAppServerProtocolTests)

#include "codex_app_server_protocol_tests.moc"
