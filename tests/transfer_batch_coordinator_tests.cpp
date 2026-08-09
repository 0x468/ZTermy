#include "application/sftp/TransferBatchCoordinator.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{

struct RemoteState final
{
    std::mutex mutex;
    std::unordered_set<std::string> directories{"/"};
    std::unordered_map<std::string, std::vector<char>> files;
};

class BatchClient final : public ztermy::sftp::SftpClient
{
public:
    explicit BatchClient(std::shared_ptr<RemoteState> state) : m_state(std::move(state)) {}

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
        std::scoped_lock lock(m_state->mutex);
        const std::string path(remotePath);
        if (m_state->directories.contains(path))
        {
            return ztermy::sftp::DirectoryEntry{.remotePath = path, .type = ztermy::sftp::EntryType::Directory};
        }
        if (const auto file = m_state->files.find(path); file != m_state->files.end())
        {
            return ztermy::sftp::DirectoryEntry{.remotePath = path,
                                                .type = ztermy::sftp::EntryType::RegularFile,
                                                .size = file->second.size()};
        }
        return std::optional<ztermy::sftp::DirectoryEntry>{};
    }
    std::expected<void, ztermy::ssh::SshTransportError> createDirectory(const std::string_view remotePath,
                                                                        const std::stop_token &) override
    {
        std::scoped_lock lock(m_state->mutex);
        m_state->directories.emplace(remotePath);
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> renameEntry(const std::string_view sourcePath,
                                                                    const std::string_view destinationPath, bool,
                                                                    const std::stop_token &) override
    {
        std::scoped_lock lock(m_state->mutex);
        const auto source = m_state->files.find(std::string(sourcePath));
        if (source == m_state->files.end())
        {
            return std::unexpected(
                ztermy::ssh::SshTransportError{.kind = ztermy::ssh::SshTransportErrorKind::ProtocolError});
        }
        m_state->files.insert_or_assign(std::string(destinationPath), std::move(source->second));
        m_state->files.erase(source);
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> removeEntry(const std::string_view remotePath, bool directory,
                                                                    const std::stop_token &) override
    {
        std::scoped_lock lock(m_state->mutex);
        if (directory)
        {
            m_state->directories.erase(std::string(remotePath));
        }
        else
        {
            m_state->files.erase(std::string(remotePath));
        }
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> openFileForRead(const std::string_view remotePath,
                                                                        const std::stop_token &) override
    {
        std::scoped_lock lock(m_state->mutex);
        m_openPath = std::string(remotePath);
        m_offset = 0;
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> openFileForWrite(const std::string_view remotePath, bool,
                                                                         const std::stop_token &) override
    {
        m_openPath = std::string(remotePath);
        m_buffer.clear();
        m_offset = 0;
        return {};
    }
    std::expected<std::size_t, ztermy::ssh::SshTransportError> readFile(const std::span<char> output,
                                                                        const std::stop_token &) override
    {
        std::scoped_lock lock(m_state->mutex);
        const auto file = m_state->files.find(m_openPath);
        if (file == m_state->files.end())
        {
            return 0U;
        }
        const std::size_t count = std::min(output.size(), file->second.size() - m_offset);
        std::ranges::copy_n(file->second.begin() + static_cast<std::ptrdiff_t>(m_offset),
                            static_cast<std::ptrdiff_t>(count), output.begin());
        m_offset += count;
        return count;
    }
    std::expected<void, ztermy::ssh::SshTransportError> writeFile(const std::span<const char> input,
                                                                  const std::stop_token &) override
    {
        m_buffer.insert(m_buffer.end(), input.begin(), input.end());
        return {};
    }
    std::expected<void, ztermy::ssh::SshTransportError> closeFile(const std::stop_token &) override
    {
        if (!m_openPath.empty() && !m_buffer.empty())
        {
            std::scoped_lock lock(m_state->mutex);
            m_state->files.insert_or_assign(m_openPath, m_buffer);
        }
        m_openPath.clear();
        m_buffer.clear();
        return {};
    }

private:
    std::shared_ptr<RemoteState> m_state;
    std::string m_openPath;
    std::vector<char> m_buffer;
    std::size_t m_offset = 0;
};

[[nodiscard]] ztermy::sftp::SftpClientFactory factory(const std::shared_ptr<RemoteState> &state)
{
    return [state](ztermy::ssh::SshConnectionRequest &, const ztermy::ssh::SshConnectionCallbacks &,
                   const std::stop_token &)
               -> std::expected<std::unique_ptr<ztermy::sftp::SftpClient>, ztermy::ssh::SshBootstrapError> {
        return std::make_unique<BatchClient>(state);
    };
}

[[nodiscard]] ztermy::sftp::TransferRequestProvider provider()
{
    return []() -> std::expected<ztermy::ssh::SshConnectionRequest, ztermy::sftp::TransferCredentialError> {
        return ztermy::ssh::SshConnectionRequest{};
    };
}

} // namespace

class TransferBatchCoordinatorTests final : public QObject
{
    Q_OBJECT

private slots:
    void plansMaterializesAndCompletesRecursiveUpload();
    void rejectsInvalidAndDuplicateBatchRequests();
};

void TransferBatchCoordinatorTests::plansMaterializesAndCompletesRecursiveUpload()
{
    using namespace ztermy::sftp;
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.filePath(QStringLiteral("project"));
    QVERIFY(QDir().mkpath(root + QStringLiteral("/empty")));
    QFile file(root + QStringLiteral("/data.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("recursive"), 9);
    file.close();

    const auto state = std::make_shared<RemoteState>();
    TransferManager manager(2, factory(state));
    TransferBatchCoordinator coordinator(manager, factory(state));
    TransferPlanRequest request{.batchId = "batch-upload",
                                .endpointId = "profile-1",
                                .displayName = "Upload project",
                                .destinationRoot = "/upload",
                                .sourceRoots = {root.toStdString()},
                                .direction = TransferBatchDirection::Upload};
    auto queued = coordinator.enqueue(std::move(request), provider(), "utf-8");
    QVERIFY(queued.has_value());
    QTRY_COMPARE_WITH_TIMEOUT(coordinator.snapshot()->front().status, TransferBatchStatus::Completed, 5000);

    std::scoped_lock lock(state->mutex);
    QVERIFY(state->directories.contains("/upload/project"));
    QVERIFY(state->directories.contains("/upload/project/empty"));
    const auto uploaded = state->files.find("/upload/project/data.txt");
    QVERIFY(uploaded != state->files.end());
    QCOMPARE(std::string(uploaded->second.begin(), uploaded->second.end()), "recursive");
    manager.shutdown();
}

void TransferBatchCoordinatorTests::rejectsInvalidAndDuplicateBatchRequests()
{
    using namespace ztermy::sftp;
    const auto state = std::make_shared<RemoteState>();
    TransferManager manager(1, factory(state));
    TransferBatchCoordinator coordinator(manager, factory(state));
    TransferPlanRequest invalid;
    QCOMPARE(coordinator.enqueue(std::move(invalid), provider(), "utf-8").error(),
             TransferBatchCoordinatorError::InvalidRequest);

    TransferPlanRequest request{.batchId = "duplicate",
                                .endpointId = "profile-1",
                                .displayName = "Missing source",
                                .destinationRoot = "/upload",
                                .sourceRoots = {"Z:/ztermy-missing-source"},
                                .direction = TransferBatchDirection::Upload};
    QVERIFY(coordinator.enqueue(request, provider(), "utf-8").has_value());
    QCOMPARE(coordinator.enqueue(std::move(request), provider(), "utf-8").error(),
             TransferBatchCoordinatorError::Capacity);
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.snapshot()->front().status == TransferBatchStatus::Failed, 5000);
    manager.shutdown();
}

QTEST_GUILESS_MAIN(TransferBatchCoordinatorTests)

#include "transfer_batch_coordinator_tests.moc"
