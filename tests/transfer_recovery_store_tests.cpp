#include "infrastructure/sftp/TransferRecoveryStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <array>
#include <string>

namespace
{

ztermy::sftp::TransferTask transfer(const std::string &id, const ztermy::sftp::TransferStatus status)
{
    return ztermy::sftp::TransferTask{
        .id = id,
        .endpointId = "profile-1",
        .displayName = id + ".txt",
        .sourcePath = "/remote/" + id,
        .destinationPath = "C:/Downloads/" + id,
        .direction = ztermy::sftp::TransferDirection::Download,
        .status = status,
        .totalBytes = 100,
        .transferredBytes = 40,
        .startedUtcMs = 123,
        .errorCode = status == ztermy::sftp::TransferStatus::Failed ? "interrupted" : "",
        .retryable = true,
    };
}

} // namespace

class TransferRecoveryStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void persistsOnlyRecoverableTasks();
    void rejectsCorruptAndUnsupportedDocuments();
    void supportsUnknownTotals();
};

void TransferRecoveryStoreTests::persistsOnlyRecoverableTasks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::sftp::TransferRecoveryStore store(directory.filePath(QStringLiteral("transfer_recovery.json")));
    const std::array tasks{
        transfer("running", ztermy::sftp::TransferStatus::Running),
        transfer("interrupted", ztermy::sftp::TransferStatus::Failed),
        transfer("completed", ztermy::sftp::TransferStatus::Completed),
    };

    QVERIFY(store.save(tasks));
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), std::size_t{2});
    for (const auto &task : *loaded)
    {
        QCOMPARE(task.status, ztermy::sftp::TransferStatus::Failed);
        QCOMPARE(task.errorCode, std::string("interrupted"));
        QVERIFY(task.retryable);
    }
}

void TransferRecoveryStoreTests::rejectsCorruptAndUnsupportedDocuments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("transfer_recovery.json"));
    ztermy::sftp::TransferRecoveryStore store(path);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not-json"), qint64{8});
    file.close();
    QCOMPARE(store.load().error(), ztermy::sftp::TransferRecoveryError::InvalidData);

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.write("{\"version\":2,\"tasks\":[]}") > 0);
    file.close();
    QCOMPARE(store.load().error(), ztermy::sftp::TransferRecoveryError::UnsupportedVersion);
}

void TransferRecoveryStoreTests::supportsUnknownTotals()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::sftp::TransferRecoveryStore store(directory.filePath(QStringLiteral("transfer_recovery.json")));
    auto task = transfer("unknown-total", ztermy::sftp::TransferStatus::Queued);
    task.totalBytes = 0;
    task.transferredBytes = 17;

    QVERIFY(store.save(std::span(&task, std::size_t{1})));
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->front().totalBytes, std::uint64_t{0});
    QCOMPARE(loaded->front().transferredBytes, std::uint64_t{17});
}

QTEST_GUILESS_MAIN(TransferRecoveryStoreTests)

#include "transfer_recovery_store_tests.moc"
