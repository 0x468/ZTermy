#include "application/logging/ConnectionHistoryController.h"
#include "infrastructure/logging/ConnectionHistoryStore.h"

#include <QTemporaryDir>
#include <QTest>

namespace
{

[[nodiscard]] ztermy::logging::ConnectionHistoryEntry entry(const std::string &id, const std::int64_t started,
                                                            const bool saved = false)
{
    return {.id = id,
            .sessionId = id,
            .hostLabel = "Gateway",
            .hostname = "192.168.1.22",
            .username = "root",
            .protocol = "ssh",
            .localUsername = "GWF",
            .localHostname = "desktop",
            .status = "connected",
            .phase = "connected",
            .startedUtcMs = started,
            .saved = saved};
}

} // namespace

class ConnectionHistoryTests final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsAndRecoversInterruptedSession();
    void filtersPaginatesAndPreservesSavedEntries();
};

void ConnectionHistoryTests::roundTripsAndRecoversInterruptedSession()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("connection-history.json"));
    const ztermy::logging::ConnectionHistory history{.entries = {entry("active", 1)}};
    QVERIFY(ztermy::logging::ConnectionHistoryStore(path).save(history));

    ztermy::logging::ConnectionHistoryController controller(path);
    QCOMPARE(controller.entries().size(), 1);
    const QVariantMap recovered = controller.entries().front().toMap();
    QCOMPARE(recovered.value(QStringLiteral("status")).toString(), QStringLiteral("interrupted"));
    QVERIFY(recovered.value(QStringLiteral("endedUtcMs")).toLongLong() >= 1);
}

void ConnectionHistoryTests::filtersPaginatesAndPreservesSavedEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("connection-history.json"));
    ztermy::logging::ConnectionHistory history;
    for (int index = 0; index < 35; ++index)
    {
        auto current = entry("session-" + std::to_string(index), 1'000 + index, index == 0);
        current.endedUtcMs = current.startedUtcMs + 1;
        current.status = "disconnected";
        history.entries.push_back(std::move(current));
    }
    QVERIFY(ztermy::logging::ConnectionHistoryStore(path).save(history));

    ztermy::logging::ConnectionHistoryController controller(path);
    QCOMPARE(controller.entries().size(), 30);
    QVERIFY(controller.hasMore());
    controller.loadMore();
    QCOMPARE(controller.entries().size(), 35);
    QVERIFY(!controller.hasMore());
    controller.setFilter(QStringLiteral("gateway"), {}, QStringLiteral("192.168.1.22"), true);
    QCOMPARE(controller.entries().size(), 1);
    QVERIFY(controller.clearUnsaved());
    QCOMPARE(controller.entries().size(), 1);
}

QTEST_GUILESS_MAIN(ConnectionHistoryTests)

#include "connection_history_tests.moc"
