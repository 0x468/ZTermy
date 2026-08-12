#include "infrastructure/ai/McpServerStore.h"

#include <QtTest/QTest>

#include <QFile>
#include <QTemporaryDir>

namespace
{
class McpServerStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsBoundedServersAndApprovals();
    void recoversMalformedPrimaryFromBackup();
    void rejectsDuplicateNamespaces();
};

void McpServerStoreTests::roundTripsBoundedServersAndApprovals()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::ai::McpServerStore store(directory.filePath(QStringLiteral("mcp-servers.json")));
    const ztermy::ai::McpServerRecord server{
        .configuration = {.identity = {.id = "files",
                                       .nameSpace = "files",
                                       .trust = ztermy::ai::McpServerTrust::execute},
                          .program = QStringLiteral("C:/tools/server.exe"),
                          .arguments = {QStringLiteral("--stdio")},
                          .workingDirectory = QStringLiteral("C:/tools")},
        .approvedTools = {{.exposedName = "mcp__files__read", .schemaDigest = std::string(64, 'a')}},
        .enabled = true};
    QVERIFY(store.save({server}).has_value());
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), std::size_t{1});
    QCOMPARE(loaded->front().configuration.identity.id, std::string("files"));
    QCOMPARE(loaded->front().approvedTools, server.approvedTools);
    QVERIFY(loaded->front().enabled);
}

void McpServerStoreTests::recoversMalformedPrimaryFromBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("mcp-servers.json"));
    ztermy::ai::McpServerStore store(path);
    const ztermy::ai::McpServerRecord first{
        .configuration = {.identity = {.id = "first",
                                       .nameSpace = "first",
                                       .trust = ztermy::ai::McpServerTrust::observe},
                          .program = QStringLiteral("C:/tools/first.exe")},
        .enabled = true};
    auto second = first;
    second.configuration.identity.id = "second";
    second.configuration.identity.nameSpace = "second";
    second.configuration.program = QStringLiteral("C:/tools/second.exe");
    QVERIFY(store.save({first}).has_value());
    QVERIFY(store.save({second}).has_value());

    QFile primary(path);
    QVERIFY(primary.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(primary.write("{malformed"), qint64{10});
    primary.close();

    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QVERIFY(store.lastLoadRecoveredFromBackup());
    QCOMPARE(loaded->size(), std::size_t{1});
    QCOMPARE(loaded->front().configuration.identity.id, std::string("first"));
}

void McpServerStoreTests::rejectsDuplicateNamespaces()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("mcp-servers.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        R"({"schema_version":1,"servers":[{"id":"one","namespace":"same","program":"a","trust":"execute"},{"id":"two","namespace":"same","program":"b","trust":"execute"}]})");
    file.close();
    ztermy::ai::McpServerStore store(path);
    QVERIFY(!store.load().has_value());
}
} // namespace

QTEST_GUILESS_MAIN(McpServerStoreTests)

#include "mcp_server_store_tests.moc"
