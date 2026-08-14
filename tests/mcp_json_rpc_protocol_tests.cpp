#include "infrastructure/ai/McpJsonRpcProtocol.h"

#include <QtTest/QTest>

namespace
{
class McpJsonRpcProtocolTests final : public QObject
{
    Q_OBJECT

private slots:
    void buildsStrictLifecycleAndCallRequests();
    void parsesFragmentedDiscoveryAndBoundsInput();
    void rejectsDiscoveryErrorsAndResetsBuffer();
};

void McpJsonRpcProtocolTests::buildsStrictLifecycleAndCallRequests()
{
    ztermy::ai::McpJsonRpcProtocol protocol;
    QVERIFY(protocol.initializeRequest(1).contains("2025-06-18"));
    QVERIFY(protocol.initializedNotification().contains("notifications/initialized"));
    QVERIFY(protocol.listToolsRequest(2).contains("tools/list"));
    auto call = protocol.callToolRequest(3, "read", R"({"path":"/tmp"})");
    QVERIFY(call.has_value());
    QVERIFY(call->contains("tools/call"));
    const QByteArray cancelled = protocol.cancelRequestNotification(3, "user cancelled");
    QVERIFY(cancelled.contains("notifications/cancelled"));
    QVERIFY(cancelled.contains("user cancelled"));
    QVERIFY(!protocol.callToolRequest(3, "read", "not-json").has_value());
}

void McpJsonRpcProtocolTests::parsesFragmentedDiscoveryAndBoundsInput()
{
    ztermy::ai::McpJsonRpcProtocol protocol;
    auto first = protocol.append(R"({"jsonrpc":"2.0","id":2,"result":{"tools":[{"name":"read",)"
                                 R"("description":"Read","inputSchema":{"type":"object"}}]}})");
    QVERIFY(first.has_value());
    QVERIFY(first->empty());
    auto second = protocol.append("\n");
    QVERIFY(second.has_value());
    QCOMPARE(second->size(), std::size_t{1});
    auto tools = ztermy::ai::McpJsonRpcProtocol::discoveredTools(second->front());
    QVERIFY(tools.has_value());
    QCOMPARE(tools->front().name, std::string("read"));
    QByteArray oversized(1024 * 1024 + 1, 'x');
    QVERIFY(!protocol.append(oversized).has_value());
}

void McpJsonRpcProtocolTests::rejectsDiscoveryErrorsAndResetsBuffer()
{
    ztermy::ai::McpJsonRpcProtocol protocol;

    // A JSON-RPC error on tools/list must be an explicit discovery failure,
    // never a silent empty success. Plain escaped strings keep the payload
    // free of raw-string delimiter traps.
    const auto errorReply = protocol.append(
        QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":2,\"error\":{\"code\":-32000,\"message\":\"permission "
                          "denied\"}}\n"));
    QVERIFY(errorReply.has_value());
    QCOMPARE(errorReply->size(), std::size_t{1});
    QVERIFY(!ztermy::ai::McpJsonRpcProtocol::discoveredTools(errorReply->front()).has_value());

    // A half-framed message left in the buffer must not leak into the next
    // server session after reset().
    const auto halfFramed = protocol.append(QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"tool"));
    QVERIFY(halfFramed.has_value());
    protocol.reset();
    auto afterReset = protocol.append("\n");
    QVERIFY(afterReset.has_value());
    QVERIFY(afterReset->empty());
}
} // namespace

QTEST_GUILESS_MAIN(McpJsonRpcProtocolTests)

#include "mcp_json_rpc_protocol_tests.moc"
