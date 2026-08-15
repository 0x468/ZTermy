#include "infrastructure/ai/AcpProtocol.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

#include <string>

namespace
{

[[nodiscard]] QJsonObject object(const QByteArray &framed)
{
    return QJsonDocument::fromJson(framed.trimmed()).object();
}

class AcpProtocolTests final : public QObject
{
    Q_OBJECT

private slots:
    void buildsV1LifecycleMessages();
    void buildsClientResponsesForNumericAndStringIds();
    void parsesFragmentedResponsesNotificationsAndRequests();
    void rejectsMalformedAndOversizedInputThenResets();
};

void AcpProtocolTests::buildsV1LifecycleMessages()
{
    ztermy::ai::AcpProtocol protocol;
    const auto initialize = protocol.initializeRequest(0, "0.3.0", true);
    QVERIFY(initialize.has_value());
    const QJsonObject initializeObject = object(*initialize);
    QCOMPARE(initializeObject.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
    QCOMPARE(initializeObject.value(QStringLiteral("method")).toString(), QStringLiteral("initialize"));
    const QJsonObject initializeParams = initializeObject.value(QStringLiteral("params")).toObject();
    QCOMPARE(initializeParams.value(QStringLiteral("protocolVersion")).toInt(), 1);
    QVERIFY(initializeParams.value(QStringLiteral("clientCapabilities"))
                .toObject()
                .value(QStringLiteral("terminal"))
                .toBool());
    QVERIFY(!initializeParams.value(QStringLiteral("clientCapabilities")).toObject().contains(QStringLiteral("fs")));

    const std::string workingDirectory = QDir::tempPath().toStdString();
    const auto session = protocol.newSessionRequest(1, workingDirectory);
    QVERIFY(session.has_value());
    const QJsonObject sessionObject = object(*session);
    QCOMPARE(sessionObject.value(QStringLiteral("method")).toString(), QStringLiteral("session/new"));
    QCOMPARE(
        sessionObject.value(QStringLiteral("params")).toObject().value(QStringLiteral("mcpServers")).toArray().size(),
        0);

    const auto prompt = protocol.promptRequest(2, "session-1", "Inspect disk usage");
    QVERIFY(prompt.has_value());
    const QJsonObject promptObject = object(*prompt);
    QCOMPARE(promptObject.value(QStringLiteral("method")).toString(), QStringLiteral("session/prompt"));
    QCOMPARE(promptObject.value(QStringLiteral("params"))
                 .toObject()
                 .value(QStringLiteral("prompt"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("text"))
                 .toString(),
             QStringLiteral("Inspect disk usage"));

    const auto cancel = protocol.cancelNotification("session-1");
    QVERIFY(cancel.has_value());
    QVERIFY(!object(*cancel).contains(QStringLiteral("id")));
    QCOMPARE(object(*cancel).value(QStringLiteral("method")).toString(), QStringLiteral("session/cancel"));

    const auto resume = protocol.resumeSessionRequest(3, "session-1", workingDirectory);
    QVERIFY(resume.has_value());
    QCOMPARE(object(*resume).value(QStringLiteral("method")).toString(), QStringLiteral("session/resume"));
    const auto close = protocol.closeSessionRequest(4, "session-1");
    QVERIFY(close.has_value());
    QCOMPARE(object(*close).value(QStringLiteral("method")).toString(), QStringLiteral("session/close"));

    QVERIFY(!protocol.newSessionRequest(5, "relative/path").has_value());
    QVERIFY(!protocol.promptRequest(6, "session-1", std::string(1024 * 1024 + 1, 'x')).has_value());
}

void AcpProtocolTests::buildsClientResponsesForNumericAndStringIds()
{
    ztermy::ai::AcpProtocol protocol;
    const auto result = protocol.resultResponse(QJsonValue(7), QJsonObject{{QStringLiteral("terminalId"), "t-1"}});
    QVERIFY(result.has_value());
    QCOMPARE(object(*result).value(QStringLiteral("id")).toInt(), 7);
    QCOMPARE(object(*result).value(QStringLiteral("result")).toObject().value(QStringLiteral("terminalId")).toString(),
             QStringLiteral("t-1"));

    const auto error =
        protocol.errorResponse(QJsonValue(QStringLiteral("permission-1")), -32601, "Unsupported ACP client method");
    QVERIFY(error.has_value());
    QCOMPARE(object(*error).value(QStringLiteral("id")).toString(), QStringLiteral("permission-1"));
    QCOMPARE(object(*error).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32601);
    QVERIFY(!protocol.resultResponse(QJsonValue(), QJsonObject{}).has_value());
}

void AcpProtocolTests::parsesFragmentedResponsesNotificationsAndRequests()
{
    ztermy::ai::AcpProtocol protocol;
    const auto first =
        protocol.append(QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":0,\"result\":{\"protocolVersion\":1}}\n"
                                          "{\"jsonrpc\":\"2.0\",\"method\":\"session/up"));
    QVERIFY(first.has_value());
    QCOMPARE(first->size(), std::size_t{1});
    QCOMPARE(first->front().kind, ztermy::ai::AcpMessageKind::response);
    QVERIFY(first->front().hasId);
    QCOMPARE(first->front().id.toInt(), 0);

    const auto second = protocol.append(QByteArrayLiteral(
        "date\",\"params\":{\"sessionId\":\"s-1\",\"update\":{\"sessionUpdate\":\"agent_message_chunk\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":\"request-1\",\"method\":\"terminal/create\",\"params\":{"
        "\"sessionId\":\"s-1\",\"command\":\"pwd\"}}\n"));
    QVERIFY(second.has_value());
    QCOMPARE(second->size(), std::size_t{2});
    QCOMPARE((*second)[0].kind, ztermy::ai::AcpMessageKind::notification);
    QCOMPARE((*second)[0].method, QStringLiteral("session/update"));
    QCOMPARE((*second)[1].kind, ztermy::ai::AcpMessageKind::request);
    QCOMPARE((*second)[1].id.toString(), QStringLiteral("request-1"));
    QCOMPARE((*second)[1].method, QStringLiteral("terminal/create"));
}

void AcpProtocolTests::rejectsMalformedAndOversizedInputThenResets()
{
    ztermy::ai::AcpProtocol protocol;
    QVERIFY(!protocol.append(QByteArrayLiteral("{\"jsonrpc\":\"1.0\",\"id\":1,\"result\":{}}\n")).has_value());
    const auto afterReset =
        protocol.append(QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"method\":\"session/update\",\"params\":{}}\n"));
    QVERIFY(afterReset.has_value());
    QCOMPARE(afterReset->size(), std::size_t{1});

    QVERIFY(!protocol.append(QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"method\":\"terminal/create\",\"result\":{}}\n"))
                 .has_value());
    QByteArray oversized(4 * 1024 * 1024 + 1, 'x');
    QVERIFY(!protocol.append(oversized).has_value());
    QByteArray unterminated(2 * 1024 * 1024 + 1, 'x');
    QVERIFY(!protocol.append(unterminated).has_value());
    const auto valid = protocol.append(
        QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":2,\"error\":{\"code\":-32000,\"message\":\"failed\"}}\n"));
    QVERIFY(valid.has_value());
    QCOMPARE(valid->front().error.value(QStringLiteral("message")).toString(), QStringLiteral("failed"));
}

} // namespace

QTEST_GUILESS_MAIN(AcpProtocolTests)

#include "acp_protocol_tests.moc"
