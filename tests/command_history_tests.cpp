#include "application/workbench/CommandHistoryController.h"
#include "domain/workbench/CommandHistoryIndex.h"
#include "infrastructure/workbench/CommandHistoryStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace ztermy::workbench;

class CommandHistoryTests final : public QObject
{
    Q_OBJECT

private slots:
    void recordDeduplicatesAndKeepsMostRecentFirst();
    void storeRoundTripsAndRejectsFutureSchema();
    void controllerMergesPendingRecordsAndRanksSuggestions();
};

void CommandHistoryTests::recordDeduplicatesAndKeepsMostRecentFirst()
{
    CommandHistoryIndex index;
    recordIndexedCommand(index, IndexedCommand{.command = "git status",
                                                .sourceId = "host-a",
                                                .sourceLabel = "Host A",
                                                .shell = ShellKind::bash,
                                                .firstUsedUtcSeconds = 10,
                                                .lastUsedUtcSeconds = 10},
                         2);
    recordIndexedCommand(index, IndexedCommand{.command = "git log",
                                                .sourceId = "host-a",
                                                .sourceLabel = "Host A",
                                                .shell = ShellKind::bash,
                                                .firstUsedUtcSeconds = 20,
                                                .lastUsedUtcSeconds = 20},
                         2);
    recordIndexedCommand(index, IndexedCommand{.command = "git status",
                                                .sourceId = "host-a",
                                                .sourceLabel = "Renamed host",
                                                .shell = ShellKind::bash,
                                                .firstUsedUtcSeconds = 30,
                                                .lastUsedUtcSeconds = 30},
                         2);

    QCOMPARE(index.entries.size(), std::size_t{2});
    QCOMPARE(index.entries.front().command, std::string("git status"));
    QCOMPARE(index.entries.front().sourceLabel, std::string("Renamed host"));
    QCOMPARE(index.entries.front().firstUsedUtcSeconds, std::int64_t{10});
    QCOMPARE(index.entries.front().lastUsedUtcSeconds, std::int64_t{30});
    QCOMPARE(index.entries.front().useCount, std::uint32_t{2});
}

void CommandHistoryTests::storeRoundTripsAndRejectsFutureSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("command-history.json"));
    CommandHistoryIndex index;
    index.entries.push_back(IndexedCommand{.command = "df -h",
                                            .sourceId = "gateway",
                                            .sourceLabel = "Gateway",
                                            .shell = ShellKind::bash,
                                            .firstUsedUtcSeconds = 10,
                                            .lastUsedUtcSeconds = 12,
                                            .useCount = 3});
    CommandHistoryStore store(path);
    QVERIFY(store.save(index).has_value());
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(*loaded, index);

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(R"({"version":99,"entries":[]})"), qint64{27});
    file.close();
    const auto future = store.load();
    QVERIFY(!future.has_value());
}

void CommandHistoryTests::controllerMergesPendingRecordsAndRanksSuggestions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("command-history.json"));
    CommandHistoryController controller(path);
    controller.record(QStringLiteral("docker ps"), ShellKind::bash, QStringLiteral("host-a"),
                      QStringLiteral("Host A"), 10);
    controller.record(QStringLiteral("docker compose ps"), ShellKind::bash, QStringLiteral("host-a"),
                      QStringLiteral("Host A"), 20);
    controller.record(QStringLiteral("docker compose ps"), ShellKind::bash, QStringLiteral("host-a"),
                      QStringLiteral("Host A"), 30);
    QTRY_VERIFY_WITH_TIMEOUT(controller.ready(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.entries().size(), 2, 3000);

    const QVariantList suggestions = controller.suggestions(QStringLiteral("dock"), 8);
    QCOMPARE(suggestions.size(), 2);
    QCOMPARE(suggestions.front().toMap().value(QStringLiteral("command")).toString(),
             QStringLiteral("docker compose ps"));
    QCOMPARE(suggestions.front().toMap().value(QStringLiteral("useCount")).toUInt(), 2U);
}

QTEST_GUILESS_MAIN(CommandHistoryTests)

#include "command_history_tests.moc"
