#include "application/sftp/TransferManager.h"
#include "infrastructure/sftp/TransferRecoveryStore.h"

#include "core/security/SensitiveByteArray.h"

#include <QByteArray>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

struct ManagerFakeState final
{
    std::mutex mutex;
    std::condition_variable_any available;
    std::atomic_int activeClients = 0;
    std::atomic_int maximumActiveClients = 0;
    std::atomic_bool releaseWrites = false;
    bool holdWrites = false;
    std::atomic_bool destinationExists = false;
};

ztermy::ssh::SshTransportError cancelledError()
{
    return ztermy::ssh::SshTransportError{.kind = ztermy::ssh::SshTransportErrorKind::Cancelled};
}

class ManagerFakeClient final : public ztermy::sftp::SftpClient
{
public:
    explicit ManagerFakeClient(std::shared_ptr<ManagerFakeState> state) : m_state(std::move(state))
    {
        const int active = m_state->activeClients.fetch_add(1) + 1;
        int maximum = m_state->maximumActiveClients.load();
        while (active > maximum && !m_state->maximumActiveClients.compare_exchange_weak(maximum, active))
        {
        }
    }

    ~ManagerFakeClient() override
    {
        m_state->activeClients.fetch_sub(1);
        m_state->available.notify_all();
    }

    std::expected<std::vector<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    listDirectory(const std::string_view, const std::stop_token &) override
    {
        return std::vector<ztermy::sftp::DirectoryEntry>{};
    }

    std::expected<std::optional<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    statEntry(const std::string_view remotePath, const std::stop_token &) override
    {
        if (!m_state->destinationExists.load() || remotePath.find(".ztermy-part-") != std::string_view::npos)
        {
            return std::optional<ztermy::sftp::DirectoryEntry>{};
        }
        return std::optional<ztermy::sftp::DirectoryEntry>{ztermy::sftp::DirectoryEntry{
            .name = "existing.txt",
            .remotePath = std::string(remotePath),
            .type = ztermy::sftp::EntryType::RegularFile,
            .size = 3,
        }};
    }

    std::expected<void, ztermy::ssh::SshTransportError> createDirectory(const std::string_view,
                                                                        const std::stop_token &) override
    {
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> renameEntry(const std::string_view, const std::string_view,
                                                                    const bool, const std::stop_token &) override
    {
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> removeEntry(const std::string_view, const bool,
                                                                    const std::stop_token &) override
    {
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> openFileForRead(const std::string_view,
                                                                        const std::stop_token &) override
    {
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> openFileForWrite(const std::string_view, const bool,
                                                                         const std::stop_token &) override
    {
        return {};
    }

    std::expected<std::size_t, ztermy::ssh::SshTransportError> readFile(const std::span<char>,
                                                                        const std::stop_token &) override
    {
        return std::size_t{0};
    }

    std::expected<void, ztermy::ssh::SshTransportError> writeFile(const std::span<const char>,
                                                                  const std::stop_token &stopToken) override
    {
        if (m_state->holdWrites)
        {
            std::unique_lock lock(m_state->mutex);
            if (!m_state->available.wait(lock, stopToken, [this] {
                    return m_state->releaseWrites.load();
                }))
            {
                return std::unexpected(cancelledError());
            }
        }
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> closeFile(const std::stop_token &) override { return {}; }

private:
    std::shared_ptr<ManagerFakeState> m_state;
};

ztermy::sftp::SftpClientFactory clientFactory(const std::shared_ptr<ManagerFakeState> &state)
{
    return [state](ztermy::ssh::SshConnectionRequest &request, const ztermy::ssh::SshConnectionCallbacks &,
                   const std::stop_token &)
               -> std::expected<std::unique_ptr<ztermy::sftp::SftpClient>, ztermy::ssh::SshBootstrapError> {
        request.secret.clear();
        return std::unique_ptr<ztermy::sftp::SftpClient>(std::make_unique<ManagerFakeClient>(state));
    };
}

ztermy::sftp::TransferRequestProvider requestProvider()
{
    return []() -> std::expected<ztermy::ssh::SshConnectionRequest, ztermy::sftp::TransferCredentialError> {
        return ztermy::ssh::SshConnectionRequest{
            .host = QStringLiteral("example.test"),
            .port = 22,
            .username = QStringLiteral("tester"),
            .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
            .secret = ztermy::security::SensitiveByteArray(QByteArray("secret")),
            .knownHostsPath = QStringLiteral("known_hosts"),
        };
    };
}

bool writeLocal(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("payload") == 7;
}

ztermy::sftp::TransferTask uploadTask(const std::string &id, const QString &source)
{
    return ztermy::sftp::TransferTask{
        .id = id,
        .endpointId = "host-1",
        .displayName = id + ".txt",
        .sourcePath = source.toUtf8().toStdString(),
        .destinationPath = "/remote/" + id + ".txt",
        .direction = ztermy::sftp::TransferDirection::Upload,
    };
}

const ztermy::sftp::TransferTask *findTask(const ztermy::sftp::TransferTasksPtr &tasks, const std::string &id)
{
    const auto found = std::ranges::find(*tasks, id, &ztermy::sftp::TransferTask::id);
    return found == tasks->end() ? nullptr : &*found;
}

class TransferManagerTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void enforcesConnectionLimitAndBackfillsAfterCancellation();
    void exposesLockedCredentialsAsNeedsAttention();
    void resumesExplicitConflictDecision();
    void restoresInterruptedTransferForExplicitRetry();
};

void TransferManagerTests::initTestCase()
{
    qRegisterMetaType<ztermy::sftp::TransferTasksPtr>();
    qRegisterMetaType<ztermy::sftp::FileConflictPtr>();
    qRegisterMetaType<ztermy::sftp::ConflictAction>();
}

void TransferManagerTests::enforcesConnectionLimitAndBackfillsAfterCancellation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QVERIFY(writeLocal(source));
    const auto state = std::make_shared<ManagerFakeState>();
    state->holdWrites = true;
    ztermy::sftp::TransferManager manager(2, clientFactory(state));

    QVERIFY(manager.enqueue(uploadTask("one", source), requestProvider()));
    QVERIFY(manager.enqueue(uploadTask("two", source), requestProvider()));
    QVERIFY(manager.enqueue(uploadTask("three", source), requestProvider()));
    QTRY_COMPARE(state->activeClients.load(), 2);
    QCOMPARE(state->maximumActiveClients.load(), 2);

    manager.cancel(QStringLiteral("one"));
    QTRY_VERIFY(findTask(manager.snapshot(), "one")->status == ztermy::sftp::TransferStatus::Cancelled);
    QTRY_COMPARE(state->activeClients.load(), 2);
    QCOMPARE(state->maximumActiveClients.load(), 2);

    state->releaseWrites.store(true);
    state->available.notify_all();
    QTRY_VERIFY_WITH_TIMEOUT(manager.snapshot()->at(1).status == ztermy::sftp::TransferStatus::Completed
                                 && manager.snapshot()->at(2).status == ztermy::sftp::TransferStatus::Completed,
                             5000);
    QCOMPARE(state->maximumActiveClients.load(), 2);
}

void TransferManagerTests::exposesLockedCredentialsAsNeedsAttention()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QVERIFY(writeLocal(source));
    const auto state = std::make_shared<ManagerFakeState>();
    ztermy::sftp::TransferManager manager(1, clientFactory(state));
    const ztermy::sftp::TransferRequestProvider locked = [] {
        return std::expected<ztermy::ssh::SshConnectionRequest, ztermy::sftp::TransferCredentialError>(
            std::unexpected(ztermy::sftp::TransferCredentialError::Locked));
    };

    QVERIFY(manager.enqueue(uploadTask("locked", source), locked));

    QTRY_VERIFY(findTask(manager.snapshot(), "locked")->status == ztermy::sftp::TransferStatus::NeedsAttention);
    QCOMPARE(findTask(manager.snapshot(), "locked")->errorCode, std::string("credential-locked"));
    QCOMPARE(state->activeClients.load(), 0);
}

void TransferManagerTests::resumesExplicitConflictDecision()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QVERIFY(writeLocal(source));
    const auto state = std::make_shared<ManagerFakeState>();
    state->destinationExists.store(true);
    ztermy::sftp::TransferManager manager(1, clientFactory(state));
    QSignalSpy conflictSpy(&manager, &ztermy::sftp::TransferManager::conflictRequired);

    QVERIFY(manager.enqueue(uploadTask("conflict", source), requestProvider()));
    QTRY_COMPARE(conflictSpy.count(), 1);
    QTRY_VERIFY(findTask(manager.snapshot(), "conflict")->status == ztermy::sftp::TransferStatus::NeedsAttention);

    manager.resolveConflict(QStringLiteral("conflict"), ztermy::sftp::ConflictAction::Skip);

    QTRY_VERIFY(findTask(manager.snapshot(), "conflict")->status == ztermy::sftp::TransferStatus::Completed);
}

void TransferManagerTests::restoresInterruptedTransferForExplicitRetry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.txt"));
    QVERIFY(writeLocal(source));
    const QString recoveryPath = directory.filePath(QStringLiteral("transfer_recovery.json"));
    auto interrupted = uploadTask("recovered", source);
    interrupted.status = ztermy::sftp::TransferStatus::Running;
    interrupted.totalBytes = 7;
    interrupted.transferredBytes = 3;
    ztermy::sftp::TransferRecoveryStore store(recoveryPath);
    QVERIFY(store.save(std::span(&interrupted, std::size_t{1})));

    const auto state = std::make_shared<ManagerFakeState>();
    ztermy::sftp::TransferManager manager(1, clientFactory(state));
    manager.enableRecovery(recoveryPath, [](const std::string &) {
        return requestProvider();
    });

    const auto recoveredSnapshot = manager.snapshot();
    const auto *restored = findTask(recoveredSnapshot, "recovered");
    QVERIFY(restored != nullptr);
    QCOMPARE(restored->status, ztermy::sftp::TransferStatus::Failed);
    QCOMPARE(restored->errorCode, std::string("interrupted"));
    manager.retry(QStringLiteral("recovered"));
    QTRY_VERIFY(findTask(manager.snapshot(), "recovered")->status == ztermy::sftp::TransferStatus::Completed);
    const auto remaining = store.load();
    QVERIFY(remaining.has_value());
    QVERIFY(remaining->empty());
}

} // namespace

QTEST_GUILESS_MAIN(TransferManagerTests)

#include "transfer_manager_tests.moc"
