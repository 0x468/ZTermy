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
} // namespace

QTEST_GUILESS_MAIN(McpJsonRpcProtocolTests)

#include "mcp_json_rpc_protocol_tests.moc"
