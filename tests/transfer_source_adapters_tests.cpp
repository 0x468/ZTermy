#include "application/sftp/TransferSourceAdapters.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TransferSourceAdaptersTests final : public QObject
{
    Q_OBJECT

private slots:
    void plansUnicodeLocalTreeAndPreservesEmptyDirectories();
    void rejectsMissingRootAndHonorsCancellation();
};

namespace
{

[[nodiscard]] std::string utf8String(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] ztermy::sftp::TransferPlanRequest request(const QString &root)
{
    return {.batchId = "batch-local",
            .endpointId = "profile-1",
            .displayName = "Upload folder",
            .destinationRoot = "/srv/upload",
            .sourceRoots = {utf8String(root)},
            .direction = ztermy::sftp::TransferBatchDirection::Upload};
}

} // namespace

void TransferSourceAdaptersTests::plansUnicodeLocalTreeAndPreservesEmptyDirectories()
{
    using namespace ztermy::sftp;
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("项目"));
    QVERIFY(QDir().mkpath(root + QStringLiteral("/空目录")));
    QFile file(root + QStringLiteral("/数据.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("hello"), 5);
    file.close();

    LocalTransferSourceTree source;
    auto planned = planTransferTree(request(root), source);
    QVERIFY(planned.has_value());
    QCOMPARE(planned->entries.size(), 3U);
    QCOMPARE(planned->entries.at(0).relativePath, utf8String(QStringLiteral("项目")));
    QCOMPARE(planned->entries.at(1).relativePath, utf8String(QStringLiteral("项目/数据.txt")));
    QCOMPARE(planned->entries.at(1).totalBytes, 5U);
    QCOMPARE(planned->entries.at(2).relativePath, utf8String(QStringLiteral("项目/空目录")));
    QCOMPARE(planned->entries.at(2).kind, TransferPlanEntryKind::Directory);
}

void TransferSourceAdaptersTests::rejectsMissingRootAndHonorsCancellation()
{
    using namespace ztermy::sftp;
    LocalTransferSourceTree source;
    auto missing = planTransferTree(request(QStringLiteral("Z:/ztermy-missing-root")), source);
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error(), TransferPlanningError::SourceUnavailable);

    std::stop_source stopped;
    stopped.request_stop();
    auto cancelled = planTransferTree(request(QStringLiteral("Z:/ztermy-missing-root")), source, stopped.get_token());
    QVERIFY(!cancelled.has_value());
    QCOMPARE(cancelled.error(), TransferPlanningError::Cancelled);
}

QTEST_GUILESS_MAIN(TransferSourceAdaptersTests)

#include "transfer_source_adapters_tests.moc"
