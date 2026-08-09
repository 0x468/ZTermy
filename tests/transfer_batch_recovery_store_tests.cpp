#include "infrastructure/sftp/TransferBatchRecoveryStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace
{

[[nodiscard]] ztermy::sftp::TransferBatch batch(const ztermy::sftp::TransferBatchStatus status)
{
    using namespace ztermy::sftp;
    return TransferBatch{
        .id = "batch-1",
        .endpointId = "profile-1",
        .displayName = "Download workspace",
        .destinationRoot = "C:/Downloads",
        .sourceRoots = {"/srv/workspace"},
        .entries =
            {
                TransferPlanEntry{.id = "root",
                                  .relativePath = "workspace",
                                  .sourcePath = "/srv/workspace",
                                  .kind = TransferPlanEntryKind::Directory,
                                  .status = TransferPlanEntryStatus::Completed},
                TransferPlanEntry{.id = "file",
                                  .parentId = "root",
                                  .relativePath = "workspace/readme.txt",
                                  .sourcePath = "/srv/workspace/readme.txt",
                                  .childTaskId = "batch-1:file",
                                  .kind = TransferPlanEntryKind::RegularFile,
                                  .status = TransferPlanEntryStatus::Running,
                                  .totalBytes = 100,
                                  .transferredBytes = 40,
                                  .depth = 1},
            },
        .direction = TransferBatchDirection::Download,
        .status = status,
        .conflictPolicy = TransferConflictPolicy::Rename,
    };
}

class TransferBatchRecoveryStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsRecoverableBatchAsInterrupted();
    void excludesTerminalBatchHistory();
    void rejectsUnsupportedDocument();
};

void TransferBatchRecoveryStoreTests::roundTripsRecoverableBatchAsInterrupted()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::sftp::TransferBatchRecoveryStore store(directory.filePath(QStringLiteral("batches.json")));
    const std::vector batches{batch(ztermy::sftp::TransferBatchStatus::Running)};

    QVERIFY(store.save(batches).has_value());
    const auto restored = store.load();
    QVERIFY(restored.has_value());
    QCOMPARE(restored->size(), std::size_t{1});
    QCOMPARE(restored->front().status, ztermy::sftp::TransferBatchStatus::Interrupted);
    QCOMPARE(restored->front().entries.at(0).status, ztermy::sftp::TransferPlanEntryStatus::Completed);
    QCOMPARE(restored->front().entries.at(1).status, ztermy::sftp::TransferPlanEntryStatus::Interrupted);
    QCOMPARE(restored->front().entries.at(1).transferredBytes, std::uint64_t{40});
    QVERIFY(ztermy::sftp::validTransferBatch(restored->front()));
}

void TransferBatchRecoveryStoreTests::excludesTerminalBatchHistory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::sftp::TransferBatchRecoveryStore store(directory.filePath(QStringLiteral("batches.json")));
    const std::vector batches{batch(ztermy::sftp::TransferBatchStatus::Completed)};

    QVERIFY(store.save(batches).has_value());
    const auto restored = store.load();
    QVERIFY(restored.has_value());
    QVERIFY(restored->empty());
}

void TransferBatchRecoveryStoreTests::rejectsUnsupportedDocument()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("batches.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(R"({"version":99,"batches":[]})") > 0);
    file.close();

    ztermy::sftp::TransferBatchRecoveryStore store(path);
    const auto restored = store.load();
    QVERIFY(!restored.has_value());
    QCOMPARE(restored.error(), ztermy::sftp::TransferBatchRecoveryError::UnsupportedVersion);
}

} // namespace

QTEST_GUILESS_MAIN(TransferBatchRecoveryStoreTests)

#include "transfer_batch_recovery_store_tests.moc"
