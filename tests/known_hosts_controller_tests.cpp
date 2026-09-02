#include "application/ssh/KnownHostsController.h"
#include "infrastructure/ssh/KnownHostsStore.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <array>
#include <cstdint>

namespace
{

[[nodiscard]] ztermy::ssh::KnownHostEntry entry(const std::uint8_t marker = 1)
{
    return {
        .endpoint = {.host = "server.example.test", .port = 22},
        .algorithm = ztermy::ssh::HostKeyAlgorithm::Ed25519,
        .encodedKey = {marker, 2, 3, 4},
    };
}

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

} // namespace

class KnownHostsControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void loadsAndRemovesStoredEntries();
    void importsOpenSshEntriesWithoutOverwritingConflicts();
};

void KnownHostsControllerTests::loadsAndRemovesStoredEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString storePath = directory.filePath(QStringLiteral("known_hosts.json"));
    const std::array entries{entry()};
    QVERIFY(ztermy::ssh::KnownHostsStore(storePath).save(entries));

    ztermy::ssh::KnownHostsController controller(storePath);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
    QCOMPARE(controller.count(), 1);
    QCOMPARE(controller.entries().front().toMap().value(QStringLiteral("endpoint")).toString(),
             QStringLiteral("server.example.test"));
    QVERIFY(controller.removeEntry(QStringLiteral("server.example.test"), 22, QStringLiteral("ssh-ed25519")));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
    QCOMPARE(controller.count(), 0);
    QVERIFY(ztermy::ssh::KnownHostsStore(storePath).load()->empty());
}

void KnownHostsControllerTests::importsOpenSshEntriesWithoutOverwritingConflicts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString storePath = directory.filePath(QStringLiteral("known_hosts.json"));
    const std::array entries{entry()};
    QVERIFY(ztermy::ssh::KnownHostsStore(storePath).save(entries));
    const QString importPath = directory.filePath(QStringLiteral("known_hosts"));
    QVERIFY(writeFile(importPath, QByteArrayLiteral("server.example.test ssh-ed25519 BQYHCA==\n"
                                                    "new.example.test ssh-ed25519 CQoLDA==\n")));

    ztermy::ssh::KnownHostsController controller(storePath);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
    QVERIFY(controller.importOpenSshFile(importPath));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
    QCOMPARE(controller.count(), 2);
    QCOMPARE(controller.operationError(), QString{});
    QCOMPARE(controller.lastImportSummary().value(QStringLiteral("added")).toULongLong(), 1ULL);
    QCOMPARE(controller.lastImportSummary().value(QStringLiteral("conflicts")).toULongLong(), 1ULL);

    const auto stored = ztermy::ssh::KnownHostsStore(storePath).load();
    QVERIFY(stored);
    QCOMPARE(stored->size(), std::size_t{2});
    QCOMPARE(stored->front().encodedKey, entry().encodedKey);
}

QTEST_GUILESS_MAIN(KnownHostsControllerTests)

#include "known_hosts_controller_tests.moc"
