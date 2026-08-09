#include "application/sftp/SftpClient.h"
#include "application/sftp/TransferBatchCoordinator.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <algorithm>
#include <atomic>
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

struct RealHostConfiguration final
{
    QString host;
    QString username;
    QString privateKeyPath;
    QString expectedFingerprint;
    std::uint16_t port = 22;
};

[[nodiscard]] std::optional<RealHostConfiguration> realHostConfiguration()
{
    if (qEnvironmentVariable("ZTERMY_TEST_SFTP_RECURSIVE") != QStringLiteral("1"))
    {
        return std::nullopt;
    }

    const QString host = qEnvironmentVariable("ZTERMY_TEST_SSH_HOST");
    const QString username = qEnvironmentVariable("ZTERMY_TEST_SSH_USERNAME");
    const QString privateKeyPath = qEnvironmentVariable("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QString expectedFingerprint = qEnvironmentVariable("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (host.isEmpty() || username.isEmpty() || privateKeyPath.isEmpty() || expectedFingerprint.isEmpty())
    {
        return std::nullopt;
    }

    bool validPort = false;
    const int configuredPort = qEnvironmentVariableIntValue("ZTERMY_TEST_SSH_PORT", &validPort);
    return RealHostConfiguration{
        .host = host,
        .username = username,
        .privateKeyPath = privateKeyPath,
        .expectedFingerprint = expectedFingerprint,
        .port = validPort && configuredPort > 0 && configuredPort <= 65'535 ? static_cast<std::uint16_t>(configuredPort)
                                                                            : std::uint16_t{22},
    };
}

[[nodiscard]] ztermy::sftp::TransferRequestProvider realHostProvider(const RealHostConfiguration &configuration,
                                                                     const QString &knownHostsPath)
{
    return
        [configuration,
         knownHostsPath]() -> std::expected<ztermy::ssh::SshConnectionRequest, ztermy::sftp::TransferCredentialError> {
            return ztermy::ssh::SshConnectionRequest{
                .host = configuration.host,
                .port = configuration.port,
                .username = configuration.username,
                .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
                .privateKeyPath = configuration.privateKeyPath,
                .knownHostsPath = knownHostsPath,
            };
        };
}

[[nodiscard]] ztermy::sftp::SftpClientFactory
realHostFactory(const QString &expectedFingerprint, const std::shared_ptr<std::atomic_bool> &fingerprintMismatch)
{
    return [expectedFingerprint, fingerprintMismatch](ztermy::ssh::SshConnectionRequest &request,
                                                      const ztermy::ssh::SshConnectionCallbacks &,
                                                      const std::stop_token &stopToken) {
        const ztermy::ssh::SshConnectionCallbacks callbacks{
            .phaseChanged = {},
            .confirmUnknownHostKey =
                [expectedFingerprint, fingerprintMismatch](const QString &, const QString &,
                                                           const QString &fingerprint) {
                    if (fingerprint != expectedFingerprint)
                    {
                        fingerprintMismatch->store(true);
                        return ztermy::ssh::UnknownHostKeyDecision::Reject;
                    }
                    return ztermy::ssh::UnknownHostKeyDecision::AcceptOnce;
                },
            .hostKeyChanged = {},
        };
        auto client = ztermy::sftp::createSftpClient(request, callbacks, stopToken);
        if (!client)
        {
            qInfo() << "SFTP client bootstrap failed: failure" << static_cast<int>(client.error().failure) << "reason"
                    << static_cast<int>(client.error().reason);
        }
        return client;
    };
}

[[nodiscard]] std::optional<ztermy::sftp::TransferBatchStatus>
batchStatus(const ztermy::sftp::TransferBatchCoordinator &coordinator, const std::string_view batchId)
{
    const auto batches = coordinator.snapshot();
    const auto batch = std::ranges::find(*batches, batchId, &ztermy::sftp::TransferBatch::id);
    if (batch == batches->end())
    {
        return std::nullopt;
    }
    return batch->status;
}

[[nodiscard]] std::optional<ztermy::sftp::TransferBatchStatus>
waitForBatchTerminalStatus(const ztermy::sftp::TransferBatchCoordinator &coordinator, const std::string_view batchId,
                           const int timeoutMilliseconds)
{
    QElapsedTimer timer;
    timer.start();
    auto status = batchStatus(coordinator, batchId);
    while (timer.elapsed() < timeoutMilliseconds
           && (!status
               || (*status != ztermy::sftp::TransferBatchStatus::Completed
                   && *status != ztermy::sftp::TransferBatchStatus::Failed)))
    {
        QTest::qWait(25);
        status = batchStatus(coordinator, batchId);
    }
    return status;
}

void reportBatchFailure(const ztermy::sftp::TransferBatchCoordinator &coordinator, const std::string_view batchId)
{
    const auto batches = coordinator.snapshot();
    const auto batch = std::ranges::find(*batches, batchId, &ztermy::sftp::TransferBatch::id);
    if (batch == batches->end())
    {
        qInfo() << "Missing transfer batch";
        return;
    }
    qInfo() << "Transfer batch status" << static_cast<int>(batch->status) << "entries" << batch->entries.size();
    for (const auto &entry : batch->entries)
    {
        if (!entry.errorCode.empty() || entry.status != ztermy::sftp::TransferPlanEntryStatus::Completed)
        {
            qInfo().noquote() << "Entry" << QString::fromStdString(entry.relativePath) << "status"
                              << static_cast<int>(entry.status) << "error" << QString::fromStdString(entry.errorCode);
        }
    }
}

} // namespace

class TransferBatchCoordinatorTests final : public QObject
{
    Q_OBJECT

private slots:
    void plansMaterializesAndCompletesRecursiveUpload();
    void rejectsInvalidAndDuplicateBatchRequests();
    void recursivelyUploadsAndDownloadsOnRealHost();
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

    const auto batch = coordinator.snapshot()->front();
    const auto child = std::ranges::find_if(batch.entries, [](const TransferPlanEntry &entry) {
        return !entry.childTaskId.empty();
    });
    QVERIFY(child != batch.entries.end());
    QVERIFY(coordinator.ownsChildTask(child->childTaskId));
    QVERIFY(!coordinator.automaticConflictPolicy(child->childTaskId).has_value());
    coordinator.setConflictPolicyForChild(child->childTaskId, TransferConflictPolicy::Skip, true);
    QCOMPARE(coordinator.automaticConflictPolicy(child->childTaskId), TransferConflictPolicy::Skip);

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

void TransferBatchCoordinatorTests::recursivelyUploadsAndDownloadsOnRealHost()
{
    using namespace ztermy::sftp;
    const auto configuration = realHostConfiguration();
    if (!configuration)
    {
        QSKIP("Set ZTERMY_TEST_SFTP_RECURSIVE=1 and the real-host private-key variables to run this gate");
    }

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString uniqueName = QStringLiteral("ztermy-v212-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString sourceRoot = temporary.filePath(uniqueName);
    const QString unicodeDirectory = sourceRoot + QStringLiteral("/中文目录");
    QVERIFY(QDir().mkpath(unicodeDirectory));
    QVERIFY(QDir().mkpath(sourceRoot + QStringLiteral("/empty")));

    QFile rootFile(sourceRoot + QStringLiteral("/root.txt"));
    QVERIFY(rootFile.open(QIODevice::WriteOnly));
    QCOMPARE(rootFile.write("ztermy recursive root\n"), 22);
    rootFile.close();
    QFile unicodeFile(unicodeDirectory + QStringLiteral("/内容.txt"));
    QVERIFY(unicodeFile.open(QIODevice::WriteOnly));
    const QByteArray unicodePayload = QStringLiteral("递归 SFTP 往返\n").toUtf8();
    QCOMPARE(unicodeFile.write(unicodePayload), unicodePayload.size());
    unicodeFile.close();

    const auto mismatch = std::make_shared<std::atomic_bool>(false);
    const auto clientFactory = realHostFactory(configuration->expectedFingerprint, mismatch);
    const auto requestProvider =
        realHostProvider(*configuration, temporary.filePath(QStringLiteral("known_hosts.json")));
    const std::string remoteRoot = QStringLiteral("/tmp/%1").arg(uniqueName).toStdString();
    const std::string remoteUnicodeDirectory = remoteRoot + "/中文目录";

    auto cleanup = qScopeGuard([&] {
        auto request = requestProvider();
        if (!request)
        {
            return;
        }
        auto client = clientFactory(*request, {}, {});
        if (!client)
        {
            return;
        }
        const auto remove = [&client](const std::string_view path, const bool directory) {
            if (const auto removed = (*client)->removeEntry(path, directory, {}); !removed)
            {
                qInfo() << "Real-host fixture cleanup skipped an entry: kind" << static_cast<int>(removed.error().kind);
            }
        };
        remove(remoteUnicodeDirectory + "/内容.txt", false);
        remove(remoteRoot + "/root.txt", false);
        remove(remoteRoot + "/empty", true);
        remove(remoteUnicodeDirectory, true);
        remove(remoteRoot, true);
    });

    TransferManager manager(2, clientFactory);
    TransferBatchCoordinator coordinator(manager, clientFactory);
    QString planningError;
    connect(&coordinator, &TransferBatchCoordinator::planningCompleted, &coordinator,
            [&planningError](const TransferBatchPlanningOutcomePtr &outcome) {
                if (outcome && !outcome->errorCode.empty())
                {
                    planningError = QString::fromStdString(outcome->errorCode);
                }
            });
    TransferPlanRequest upload{
        .batchId = "real-upload",
        .endpointId = "real-host",
        .displayName = "Recursive real-host upload",
        .destinationRoot = "/tmp",
        .sourceRoots = {sourceRoot.toStdString()},
        .direction = TransferBatchDirection::Upload,
    };
    QVERIFY(coordinator.enqueue(std::move(upload), requestProvider, "utf-8").has_value());
    const auto uploadStatus = waitForBatchTerminalStatus(coordinator, "real-upload", 30'000);
    if (uploadStatus != TransferBatchStatus::Completed)
    {
        reportBatchFailure(coordinator, "real-upload");
        qInfo().noquote() << "Planning error" << planningError;
        QFAIL("Recursive real-host upload did not complete");
    }
    QVERIFY(!mismatch->load());

    const QString downloadRoot = temporary.filePath(QStringLiteral("download"));
    QVERIFY(QDir().mkpath(downloadRoot));
    TransferPlanRequest download{
        .batchId = "real-download",
        .endpointId = "real-host",
        .displayName = "Recursive real-host download",
        .destinationRoot = downloadRoot.toStdString(),
        .sourceRoots = {remoteRoot},
        .direction = TransferBatchDirection::Download,
    };
    planningError.clear();
    QVERIFY(coordinator.enqueue(std::move(download), requestProvider, "utf-8").has_value());
    const auto downloadStatus = waitForBatchTerminalStatus(coordinator, "real-download", 30'000);
    if (downloadStatus != TransferBatchStatus::Completed)
    {
        reportBatchFailure(coordinator, "real-download");
        qInfo().noquote() << "Planning error" << planningError;
        QFAIL("Recursive real-host download did not complete");
    }
    QVERIFY(!mismatch->load());

    QFile downloadedRoot(downloadRoot + QLatin1Char('/') + uniqueName + QStringLiteral("/root.txt"));
    QVERIFY(downloadedRoot.open(QIODevice::ReadOnly));
    QCOMPARE(downloadedRoot.readAll(), QByteArrayLiteral("ztermy recursive root\n"));
    QFile downloadedUnicode(downloadRoot + QLatin1Char('/') + uniqueName + QStringLiteral("/中文目录/内容.txt"));
    QVERIFY(downloadedUnicode.open(QIODevice::ReadOnly));
    QCOMPARE(downloadedUnicode.readAll(), unicodePayload);
    QVERIFY(QDir(downloadRoot + QLatin1Char('/') + uniqueName + QStringLiteral("/empty")).exists());
    manager.shutdown();
}

QTEST_GUILESS_MAIN(TransferBatchCoordinatorTests)

#include "transfer_batch_coordinator_tests.moc"
