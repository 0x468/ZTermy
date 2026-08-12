#include "infrastructure/ai/McpStdioClient.h"

#include <QtTest/QTest>

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <optional>

namespace
{
class McpStdioClientTests final : public QObject
{
    Q_OBJECT

private slots:
    void discoversApprovesAndCallsTool();
};

void McpStdioClientTests::discoversApprovesAndCallsTool()
{
    ztermy::ai::McpToolRegistry registry;
    ztermy::ai::McpStdioClient client(registry);
    const QString helper = QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_mcp_test_server.exe");
    QEventLoop discoveryLoop;
    std::optional<ztermy::ai::McpDiscoveryUpdate> update;
    QString failure;
    auto started = client.start(
        ztermy::ai::McpStdioConfiguration{
            .identity = {.id = "test-server", .nameSpace = "test", .trust = ztermy::ai::McpServerTrust::execute},
            .program = helper},
        [&discoveryLoop, &update, &failure](auto result) {
            if (result.has_value())
            {
                update = std::move(*result);
            }
            else
            {
                failure = result.error();
            }
            discoveryLoop.quit();
        });
    QVERIFY(started.has_value());
    QTimer::singleShot(5'000, &discoveryLoop, &QEventLoop::quit);
    discoveryLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(update.has_value());
    QVERIFY(update->reviewRequired);
    const auto &tool = update->tools.front();
    QVERIFY(registry.approve(tool.serverId, tool.exposedName, tool.schemaDigest));
    QCOMPARE(registry.definitions().size(), std::size_t{1});

    QEventLoop callLoop;
    std::string output;
    auto called = client.call(tool.exposedName, R"({"text":"hello"})", [&callLoop, &output](auto result) {
        if (result.has_value())
        {
            output = std::move(*result);
        }
        callLoop.quit();
    });
    QVERIFY(called.has_value());
    QVERIFY(!client.cancel(*called + 100));
    QTimer::singleShot(5'000, &callLoop, &QEventLoop::quit);
    callLoop.exec();
    QVERIFY(!output.empty());
    const QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromStdString(output)).object();
    QVERIFY(envelope.value(QStringLiteral("ok")).toBool());
    QVERIFY(envelope.value(QStringLiteral("untrusted_evidence")).toBool());

    const auto slow = update->tools.at(1);
    QVERIFY(registry.approve(slow.serverId, slow.exposedName, slow.schemaDigest));
    bool cancelledHandlerCalled = false;
    auto slowCall = client.call(slow.exposedName, R"({})", [&cancelledHandlerCalled](auto result) {
        cancelledHandlerCalled = true;
        QVERIFY(!result.has_value());
    });
    QVERIFY(slowCall.has_value());
    QVERIFY(client.cancel(*slowCall));
    QVERIFY(cancelledHandlerCalled);
    client.stop();
    QVERIFY(registry.definitions().empty());
}
} // namespace

QTEST_GUILESS_MAIN(McpStdioClientTests)

#include "mcp_stdio_client_tests.moc"
