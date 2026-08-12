#include "application/ai/McpRuntimeManager.h"

#include <QtTest/QTest>

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

namespace
{
class AiMcpLifecycleStressTests final : public QObject
{
    Q_OBJECT

private slots:
    void repeatedlyStartsCallsCancelsAndStops();
};

void AiMcpLifecycleStressTests::repeatedlyStartsCallsCancelsAndStops()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString helper = QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_mcp_test_server.exe");
    constexpr int cycles = 32;
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        const QString storePath = directory.filePath(QStringLiteral("mcp-%1.json").arg(cycle));
        ztermy::ai::McpServerStore store(storePath);
        QVERIFY(store
                    .save({ztermy::ai::McpServerRecord{
                        .configuration = {.identity = {.id = "stress",
                                                       .nameSpace = "stress",
                                                       .trust = ztermy::ai::McpServerTrust::execute},
                                          .program = helper},
                        .enabled = true}})
                    .has_value());

        ztermy::ai::McpRuntimeManager manager(storePath);
        manager.initialize();
        QTRY_COMPARE_WITH_TIMEOUT(manager.tools().size(), std::size_t{2}, 5'000);
        const auto tools = manager.tools();
        const auto echo = tools.at(0).tool;
        const auto slow = tools.at(1).tool;
        QVERIFY(manager.setToolApproved(echo.serverId, echo.exposedName, echo.schemaDigest, true));
        QVERIFY(manager.setToolApproved(slow.serverId, slow.exposedName, slow.schemaDigest, true));

        QEventLoop echoLoop;
        std::string echoOutput;
        auto echoCall = manager.call(echo.exposedName, R"({"text":"stress"})", [&echoLoop, &echoOutput](auto result) {
            if (result.has_value())
            {
                echoOutput = std::move(*result);
            }
            echoLoop.quit();
        });
        QVERIFY(echoCall.has_value());
        QTimer::singleShot(5'000, &echoLoop, &QEventLoop::quit);
        echoLoop.exec();
        const QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromStdString(echoOutput)).object();
        QVERIFY(envelope.value(QStringLiteral("ok")).toBool());

        bool cancelled = false;
        auto slowCall = manager.call(slow.exposedName, R"({})", [&cancelled](auto result) {
            cancelled = !result.has_value();
        });
        QVERIFY(slowCall.has_value());
        QVERIFY(manager.cancel(*slowCall, "stress cancellation"));
        QVERIFY(cancelled);
        if ((cycle % 8) == 7)
        {
            QVERIFY(manager.restartServer("stress"));
            QTRY_COMPARE_WITH_TIMEOUT(manager.tools().size(), std::size_t{2}, 5'000);
        }
        manager.shutdown();
        QVERIFY(manager.definitions().empty());
    }
}
} // namespace

QTEST_GUILESS_MAIN(AiMcpLifecycleStressTests)

#include "ai_mcp_lifecycle_stress_tests.moc"
