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
class McpRuntimeManagerTests final : public QObject
{
    Q_OBJECT

private slots:
    void restoresReviewedToolsAndCallsExactServer();
};

void McpRuntimeManagerTests::restoresReviewedToolsAndCallsExactServer()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString storePath = directory.filePath(QStringLiteral("mcp_servers.json"));
    const QString helper = QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_mcp_test_server.exe");
    ztermy::ai::McpServerStore store(storePath);
    QVERIFY(store
                .save({ztermy::ai::McpServerRecord{
                    .configuration =
                        {.identity = {.id = "test", .nameSpace = "test", .trust = ztermy::ai::McpServerTrust::execute},
                         .program = helper},
                    .enabled = true}})
                .has_value());

    ztermy::ai::McpRuntimeManager manager(storePath);
    manager.initialize();
    QTRY_COMPARE_WITH_TIMEOUT(manager.tools().size(), std::size_t{2}, 5'000);
    const auto echo = manager.tools().front().tool;
    QVERIFY(manager.setToolApproved(echo.serverId, echo.exposedName, echo.schemaDigest, true));
    QCOMPARE(manager.definitions().size(), std::size_t{1});

    QEventLoop callLoop;
    std::string output;
    auto called = manager.call(echo.exposedName, R"({"text":"hello"})", [&callLoop, &output](auto result) {
        if (result.has_value())
        {
            output = std::move(*result);
        }
        callLoop.quit();
    });
    QVERIFY(called.has_value());
    QTimer::singleShot(5'000, &callLoop, &QEventLoop::quit);
    callLoop.exec();
    QVERIFY(QJsonDocument::fromJson(QByteArray::fromStdString(output))
                .object()
                .value(QStringLiteral("untrusted_evidence"))
                .toBool());
    manager.shutdown();

    ztermy::ai::McpRuntimeManager restored(storePath);
    restored.initialize();
    QTRY_COMPARE_WITH_TIMEOUT(restored.tools().size(), std::size_t{2}, 5'000);
    QCOMPARE(restored.definitions().size(), std::size_t{1});
    restored.shutdown();
}
} // namespace

QTEST_GUILESS_MAIN(McpRuntimeManagerTests)

#include "mcp_runtime_manager_tests.moc"
