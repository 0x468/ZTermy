#include "domain/sftp/TransferBatch.h"

#include <QTest>

#include <limits>

class TransferBatchTests final : public QObject
{
    Q_OBJECT

private slots:
    void validatesPathsAndTreeOrdering();
    void appendsFinalizesAndSummarizesPlan();
    void enforcesEntryTransitionsAndProgress();
    void saturatesAggregateByteCounters();
};

namespace
{

[[nodiscard]] ztermy::sftp::TransferBatch makeBatch()
{
    return {
        .id = "batch-1",
        .endpointId = "profile-1",
        .displayName = "Download project",
        .destinationRoot = R"(C:\Downloads)",
        .sourceRoots = {"/srv/project"},
        .direction = ztermy::sftp::TransferBatchDirection::Download,
    };
}

} // namespace

void TransferBatchTests::validatesPathsAndTreeOrdering()
{
    using namespace ztermy::sftp;
    QVERIFY(validTransferRelativePath("project/src/main.cpp"));
    QVERIFY(validTransferRelativePath("中文/文件.txt"));
    QVERIFY(!validTransferRelativePath("/absolute"));
    QVERIFY(!validTransferRelativePath("../escape"));
    QVERIFY(!validTransferRelativePath("directory/./file"));
    QVERIFY(!validTransferRelativePath("directory//file"));
    QVERIFY(!validTransferRelativePath("trailing/"));

    TransferBatch batch = makeBatch();
    QVERIFY(appendTransferPlanEntry(batch, {.id = "root",
                                            .relativePath = "project",
                                            .sourcePath = "/srv/project",
                                            .kind = TransferPlanEntryKind::Directory})
                .has_value());
    QCOMPARE(appendTransferPlanEntry(batch, {.id = "orphan",
                                             .parentId = "missing",
                                             .relativePath = "project/orphan",
                                             .sourcePath = "/srv/project/orphan",
                                             .depth = 1})
                 .error(),
             TransferBatchError::MissingParent);
    QCOMPARE(appendTransferPlanEntry(batch, {.id = "wrong-depth",
                                             .parentId = "root",
                                             .relativePath = "project/wrong-depth",
                                             .sourcePath = "/srv/project/wrong-depth",
                                             .depth = 2})
                 .error(),
             TransferBatchError::DepthMismatch);
    QVERIFY(appendTransferPlanEntry(batch, {.id = "file",
                                            .parentId = "root",
                                            .relativePath = "project/file.txt",
                                            .sourcePath = "/srv/project/file.txt",
                                            .kind = TransferPlanEntryKind::RegularFile,
                                            .totalBytes = 42,
                                            .depth = 1})
                .has_value());
    QCOMPARE(appendTransferPlanEntry(batch, {.id = "child-of-file",
                                             .parentId = "file",
                                             .relativePath = "project/file.txt/child",
                                             .sourcePath = "/srv/project/file.txt/child",
                                             .depth = 2})
                 .error(),
             TransferBatchError::ParentNotDirectory);
    QCOMPARE(appendTransferPlanEntry(batch, {.id = "file",
                                             .parentId = "root",
                                             .relativePath = "project/other.txt",
                                             .sourcePath = "/srv/project/other.txt",
                                             .depth = 1})
                 .error(),
             TransferBatchError::DuplicateEntryId);
    QCOMPARE(appendTransferPlanEntry(batch, {.id = "other",
                                             .parentId = "root",
                                             .relativePath = "project/file.txt",
                                             .sourcePath = "/srv/project/file.txt",
                                             .depth = 1})
                 .error(),
             TransferBatchError::DuplicateRelativePath);
    QVERIFY(validTransferBatch(batch));
}

void TransferBatchTests::appendsFinalizesAndSummarizesPlan()
{
    using namespace ztermy::sftp;
    TransferBatch batch = makeBatch();
    QVERIFY(appendTransferPlanEntry(batch, {.id = "root",
                                            .relativePath = "project",
                                            .sourcePath = "/srv/project",
                                            .kind = TransferPlanEntryKind::Directory})
                .has_value());
    QVERIFY(appendTransferPlanEntry(batch, {.id = "link",
                                            .parentId = "root",
                                            .relativePath = "project/latest",
                                            .sourcePath = "/srv/project/latest",
                                            .kind = TransferPlanEntryKind::SymbolicLink,
                                            .depth = 1})
                .has_value());
    QVERIFY(appendTransferPlanEntry(batch, {.id = "file",
                                            .parentId = "root",
                                            .relativePath = "project/data.bin",
                                            .sourcePath = "/srv/project/data.bin",
                                            .kind = TransferPlanEntryKind::RegularFile,
                                            .totalBytes = 100,
                                            .depth = 1})
                .has_value());
    QCOMPARE(summarizeTransferBatch(batch).discoveryComplete, false);
    QVERIFY(finalizeTransferDiscovery(batch).has_value());
    QCOMPARE(batch.status, TransferBatchStatus::Ready);
    QCOMPARE(batch.entries.at(1).status, TransferPlanEntryStatus::Skipped);
    const TransferBatchSummary summary = summarizeTransferBatch(batch);
    QCOMPARE(summary.entryCount, 3U);
    QCOMPARE(summary.directoryCount, 1U);
    QCOMPARE(summary.regularFileCount, 1U);
    QCOMPARE(summary.skippedCount, 1U);
    QCOMPARE(summary.totalBytes, 100U);
    QVERIFY(summary.discoveryComplete);
    QVERIFY(validTransferBatch(batch));

    TransferBatch failed = makeBatch();
    QVERIFY(finalizeTransferDiscovery(failed, "entry-limit").has_value());
    QCOMPARE(failed.status, TransferBatchStatus::Failed);
    QVERIFY(validTransferBatch(failed));
}

void TransferBatchTests::enforcesEntryTransitionsAndProgress()
{
    using namespace ztermy::sftp;
    TransferBatch batch = makeBatch();
    QVERIFY(appendTransferPlanEntry(batch, {.id = "file",
                                            .relativePath = "file.bin",
                                            .sourcePath = "/srv/file.bin",
                                            .kind = TransferPlanEntryKind::RegularFile,
                                            .totalBytes = 100})
                .has_value());
    QVERIFY(finalizeTransferDiscovery(batch).has_value());
    QVERIFY(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Queued).has_value());
    QVERIFY(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Running, 40).has_value());
    QCOMPARE(summarizeTransferBatch(batch).transferredBytes, 40U);
    QCOMPARE(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Completed, 101).error(),
             TransferBatchError::InvalidTransition);
    QVERIFY(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Interrupted, 40).has_value());
    QVERIFY(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Queued, 40).has_value());
    QVERIFY(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Running, 40).has_value());
    QVERIFY(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Failed, 40, "remote-io").has_value());
    QVERIFY(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Queued, 40).has_value());
    QVERIFY(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Running, 40).has_value());
    QVERIFY(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Completed, 100).has_value());
    QCOMPARE(batch.entries.front().transferredBytes, 100U);
    QCOMPARE(updateTransferPlanEntry(batch, "file", TransferPlanEntryStatus::Queued).error(),
             TransferBatchError::InvalidTransition);
}

void TransferBatchTests::saturatesAggregateByteCounters()
{
    using namespace ztermy::sftp;
    TransferBatch batch = makeBatch();
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    QVERIFY(appendTransferPlanEntry(batch, {.id = "first",
                                            .relativePath = "first.bin",
                                            .sourcePath = "/srv/first.bin",
                                            .kind = TransferPlanEntryKind::RegularFile,
                                            .totalBytes = maximum,
                                            .transferredBytes = maximum})
                .has_value());
    QVERIFY(appendTransferPlanEntry(batch, {.id = "second",
                                            .relativePath = "second.bin",
                                            .sourcePath = "/srv/second.bin",
                                            .kind = TransferPlanEntryKind::RegularFile,
                                            .totalBytes = 10,
                                            .transferredBytes = 5})
                .has_value());
    const TransferBatchSummary summary = summarizeTransferBatch(batch);
    QCOMPARE(summary.totalBytes, maximum);
    QCOMPARE(summary.transferredBytes, maximum);
}

QTEST_GUILESS_MAIN(TransferBatchTests)

#include "transfer_batch_tests.moc"
