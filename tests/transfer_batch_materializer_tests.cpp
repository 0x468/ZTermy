#include "application/sftp/TransferBatchMaterializer.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

class DirectoryClient final : public ztermy::sftp::SftpClient
{
public:
    std::unordered_map<std::string, ztermy::sftp::DirectoryEntry> entries;
    std::vector<std::string> created;

    std::expected<std::string, ztermy::ssh::SshTransportError> canonicalizePath(const std::string_view remotePath,
                                                                                const std::stop_token &) override
    {
        return std::string(remotePath);
    }
    std::expected<std::vector<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    listDirectory(std::string_view, const std::stop_token &) override
    {
        return std::vector<ztermy::sftp::DirectoryEntry>{};
    }
    std::expected<std::optional<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    statEntry(const std::string_view remotePath, const std::stop_token &) override
    {
        const auto found = entries.find(std::string(remotePath));
        return found == entries.end() ? std::optional<ztermy::sftp::DirectoryEntry>{}
                                      : std::optional<ztermy::sftp::DirectoryEntry>{found->second};
    }
    std::expected<void, ztermy::ssh::SshTransportError> createDirectory(const std::string_view remotePath,
                                                                        const std::stop_token &) override
    {
        created.emplace_back(remotePath);
        entries.emplace(std::string(remotePath),
                        ztermy::sftp::DirectoryEntry{.remotePath = std::string(remotePath),
                                                     .type = ztermy::sftp::EntryType::Directory});
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> renameEntry(std::string_view, std::string_view, bool,
                                                                    const std::stop_token &) override
    {
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> removeEntry(std::string_view, bool,
                                                                    const std::stop_token &) override
    {
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> openFileForRead(std::string_view,
                                                                        const std::stop_token &) override
    {
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> openFileForWrite(std::string_view, bool,
                                                                         const std::stop_token &) override
    {
        return {};
    }
    std::expected<std::size_t, ztermy::ssh::SshTransportError> readFile(std::span<char>,
                                                                        const std::stop_token &) override
    {
        return 0U;
    }
    std::expected<void, ztermy::ssh::SshTransportError> writeFile(std::span<const char>,
                                                                  const std::stop_token &) override
    {
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> closeFile(const std::stop_token &) override { return {}; }
};

[[nodiscard]] std::expected<ztermy::sftp::TransferBatch, ztermy::sftp::TransferBatchError>
batch(ztermy::sftp::TransferBatchDirection direction, std::string destination)
{
    using namespace ztermy::sftp;
    TransferBatch result{
        .id = "batch-1",
        .endpointId = "profile-1",
        .displayName = "Project",
        .destinationRoot = std::move(destination),
        .sourceRoots = {direction == TransferBatchDirection::Upload ? R"(C:\source\project)" : "/srv/project"},
        .direction = direction};
    if (auto appended = appendTransferPlanEntry(result, {.id = "entry-1",
                                                         .relativePath = "project",
                                                         .sourcePath = result.sourceRoots.front(),
                                                         .kind = TransferPlanEntryKind::Directory});
        !appended)
    {
        return std::unexpected(appended.error());
    }
    if (auto appended = appendTransferPlanEntry(result, {.id = "entry-2",
                                                         .parentId = "entry-1",
                                                         .relativePath = "project/empty",
                                                         .sourcePath = result.sourceRoots.front() + "/empty",
                                                         .kind = TransferPlanEntryKind::Directory,
                                                         .depth = 1});
        !appended)
    {
        return std::unexpected(appended.error());
    }
    if (auto appended = appendTransferPlanEntry(result, {.id = "entry-3",
                                                         .parentId = "entry-1",
                                                         .relativePath = "project/data.bin",
                                                         .sourcePath = result.sourceRoots.front() + "/data.bin",
                                                         .kind = TransferPlanEntryKind::RegularFile,
                                                         .totalBytes = 11,
                                                         .depth = 1});
        !appended)
    {
        return std::unexpected(appended.error());
    }
    if (auto finalized = finalizeTransferDiscovery(result); !finalized)
    {
        return std::unexpected(finalized.error());
    }
    return result;
}

} // namespace

class TransferBatchMaterializerTests final : public QObject
{
    Q_OBJECT

private slots:
    void createsLocalDirectoriesBeforeDownloadTasks();
    void createsRemoteDirectoriesBeforeUploadTasks();
    void rejectsRemoteDirectoryCollision();
};

void TransferBatchMaterializerTests::createsLocalDirectoriesBeforeDownloadTasks()
{
    using namespace ztermy::sftp;
    QTemporaryDir destination;
    QVERIFY(destination.isValid());
    auto candidate = batch(TransferBatchDirection::Download, destination.path().toStdString());
    QVERIFY(candidate.has_value());
    TransferBatch value = std::move(*candidate);
    auto tasks = materializeTransferBatch(value, nullptr, "utf-8");
    QVERIFY(tasks.has_value());
    QCOMPARE(tasks->size(), 1U);
    QVERIFY(QDir(destination.filePath(QStringLiteral("project/empty"))).exists());
    QCOMPARE(tasks->front().direction, TransferDirection::Download);
    QCOMPARE(tasks->front().destinationPath,
             QDir::cleanPath(destination.filePath(QStringLiteral("project/data.bin"))).toStdString());
    QCOMPARE(value.status, TransferBatchStatus::Running);
    QCOMPARE(value.entries.at(0).status, TransferPlanEntryStatus::Completed);
}

void TransferBatchMaterializerTests::createsRemoteDirectoriesBeforeUploadTasks()
{
    using namespace ztermy::sftp;
    DirectoryClient client;
    auto candidate = batch(TransferBatchDirection::Upload, "/srv/upload");
    QVERIFY(candidate.has_value());
    TransferBatch value = std::move(*candidate);
    auto tasks = materializeTransferBatch(value, &client, "utf-8");
    QVERIFY(tasks.has_value());
    QCOMPARE(client.created, std::vector<std::string>({"/srv/upload/project", "/srv/upload/project/empty"}));
    QCOMPARE(tasks->size(), 1U);
    QCOMPARE(tasks->front().destinationPath, "/srv/upload/project/data.bin");
    QCOMPARE(tasks->front().id, "batch-1:entry-3");
}

void TransferBatchMaterializerTests::rejectsRemoteDirectoryCollision()
{
    using namespace ztermy::sftp;
    DirectoryClient client;
    client.entries.emplace(
        "/srv/upload/project",
        DirectoryEntry{.name = "project", .remotePath = "/srv/upload/project", .type = EntryType::RegularFile});
    auto candidate = batch(TransferBatchDirection::Upload, "/srv/upload");
    QVERIFY(candidate.has_value());
    TransferBatch value = std::move(*candidate);
    auto tasks = materializeTransferBatch(value, &client, "utf-8");
    QVERIFY(!tasks.has_value());
    QCOMPARE(tasks.error(), TransferMaterializationError::DestinationConflict);
    QCOMPARE(value.status, TransferBatchStatus::Failed);
    QCOMPARE(value.entries.front().status, TransferPlanEntryStatus::Failed);
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    TransferBatchMaterializerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "transfer_batch_materializer_tests.moc"
