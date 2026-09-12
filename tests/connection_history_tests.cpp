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
    void stopsRecordingWithoutDeletingOrBackfilling();
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

void ConnectionHistoryTests::stopsRecordingWithoutDeletingOrBackfilling()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("history.json"));
    {
        ztermy::logging::ConnectionHistoryController controller(path);
        controller.recordStarted(entry("before", 1));
        controller.setRecordingEnabled(false);
        const auto stopped = controller.entries();
        QCOMPARE(stopped.size(), 1);
        QCOMPARE(stopped.front().toMap().value(QStringLiteral("phase")).toString(),
                 QStringLiteral("recording-stopped"));
        QVERIFY(stopped.front().toMap().value(QStringLiteral("endedUtcMs")).toLongLong() > 0);
        controller.recordStarted(entry("disabled", 2));
        controller.recordPhase(QStringLiteral("before"), QStringLiteral("connected"), QStringLiteral("connected"));
        controller.recordEnded(QStringLiteral("before"));
        controller.setRawLogPath(QStringLiteral("before"), QStringLiteral("not-recorded.log"));
        QCOMPARE(controller.entries(), stopped);
        controller.setRecordingEnabled(true);
        controller.recordPhase(QStringLiteral("before"), QStringLiteral("connected"), QStringLiteral("connected"));
        controller.recordEnded(QStringLiteral("disabled"));
        QCOMPARE(controller.entries(), stopped);
        controller.recordStarted(entry("after", 3));
        controller.recordEnded(QStringLiteral("after"));
        QCOMPARE(controller.entries().size(), 2);
    } // Destruction must flush the last queued snapshot, not discard it.
    auto stored = ztermy::logging::ConnectionHistoryStore(path).load();
    QVERIFY(stored);
    QCOMPARE(stored->entries.size(), 2);
    QCOMPARE(stored->entries.front().sessionId, std::string("after"));
    QVERIFY(stored->entries.front().endedUtcMs > 0);
    {
        ztermy::logging::ConnectionHistoryController controller(path);
        controller.setRecordingEnabled(false);
        QCOMPARE(controller.entries().size(), 2);
        QVERIFY(controller.remove(QStringLiteral("before"))); // Explicit management remains available.
    }
    stored = ztermy::logging::ConnectionHistoryStore(path).load();
    QVERIFY(stored);
    QCOMPARE(stored->entries.size(), 1);
}

QTEST_GUILESS_MAIN(ConnectionHistoryTests)

#include "connection_history_tests.moc"
