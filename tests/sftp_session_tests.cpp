#include "application/sftp/SftpSession.h"

#include "core/security/SensitiveByteArray.h"

#include <QByteArray>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{

struct FakeState final
{
    std::mutex mutex;
    std::condition_variable_any available;
    std::atomic_bool slowListEntered = false;
    std::atomic_bool releaseSlowList = false;
    std::vector<std::string> created;
    std::vector<std::string> createdFiles;
    std::vector<std::pair<std::string, std::string>> renamed;
    std::vector<std::pair<std::string, bool>> removed;
};

class FakeSftpClient final : public ztermy::sftp::SftpClient
{
public:
    explicit FakeSftpClient(std::shared_ptr<FakeState> state) : m_state(std::move(state)) {}

    std::expected<std::string, ztermy::ssh::SshTransportError> canonicalizePath(const std::string_view remotePath,
                                                                                const std::stop_token &) override
    {
        return remotePath == "." ? std::string("/home/tester") : std::string(remotePath);
    }

    std::expected<std::vector<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    listDirectory(const std::string_view remotePath, const std::stop_token &stopToken) override
    {
        if (remotePath == "/slow")
        {
            m_state->slowListEntered.store(true);
            m_state->available.notify_all();
            std::unique_lock lock(m_state->mutex);
            if (!m_state->available.wait(lock, stopToken, [this] {
                    return m_state->releaseSlowList.load();
                }))
            {
                return std::unexpected(ztermy::ssh::SshTransportError{
                    .kind = ztermy::ssh::SshTransportErrorKind::Cancelled,
                });
            }
        }
        return std::vector<ztermy::sftp::DirectoryEntry>{ztermy::sftp::DirectoryEntry{
            .name = "entry",
            .remotePath = std::string(remotePath) + "/entry",
            .type = ztermy::sftp::EntryType::RegularFile,
            .size = 42,
        }};
    }

    std::expected<std::optional<ztermy::sftp::DirectoryEntry>, ztermy::ssh::SshTransportError>
    statEntry(const std::string_view, const std::stop_token &) override
    {
        return std::optional<ztermy::sftp::DirectoryEntry>{};
    }

    std::expected<void, ztermy::ssh::SshTransportError> createDirectory(const std::string_view remotePath,
                                                                        const std::stop_token &) override
    {
        std::scoped_lock lock(m_state->mutex);
        m_state->created.emplace_back(remotePath);
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> renameEntry(const std::string_view sourcePath,
                                                                    const std::string_view destinationPath, const bool,
                                                                    const std::stop_token &) override
    {
        std::scoped_lock lock(m_state->mutex);
        m_state->renamed.emplace_back(sourcePath, destinationPath);
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError>
    removeEntry(const std::string_view remotePath, const bool directory, const std::stop_token &) override
    {
        std::scoped_lock lock(m_state->mutex);
        m_state->removed.emplace_back(remotePath, directory);
        return {};
    }

    std::expected<void, ztermy::ssh::SshTransportError> openFileForRead(const std::string_view,
                                                                        const std::stop_token &) override
    {
        return std::unexpected(
            ztermy::ssh::SshTransportError{.kind = ztermy::ssh::SshTransportErrorKind::InvalidState});
    }

    std::expected<void, ztermy::ssh::SshTransportError> openFileForWrite(const std::string_view remotePath, const bool,
                                                                         const std::stop_token &) override
    {
        std::scoped_lock lock(m_state->mutex);
        m_state->createdFiles.emplace_back(remotePath);
        return {};
    }

    std::expected<std::size_t, ztermy::ssh::SshTransportError> readFile(const std::span<char>,
                                                                        const std::stop_token &) override
    {
        return std::unexpected(
            ztermy::ssh::SshTransportError{.kind = ztermy::ssh::SshTransportErrorKind::InvalidState});
    }

    std::expected<void, ztermy::ssh::SshTransportError> writeFile(const std::span<const char>,
                                                                  const std::stop_token &) override
    {
        return std::unexpected(
            ztermy::ssh::SshTransportError{.kind = ztermy::ssh::SshTransportErrorKind::InvalidState});
    }

    std::expected<void, ztermy::ssh::SshTransportError> closeFile(const std::stop_token &) override { return {}; }

private:
    std::shared_ptr<FakeState> m_state;
};

ztermy::ssh::SshConnectionRequest validRequest()
{
    return ztermy::ssh::SshConnectionRequest{
        .host = QStringLiteral("example.test"),
        .port = 22,
        .username = QStringLiteral("tester"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
        .secret = ztermy::security::SensitiveByteArray(QByteArray("secret")),
        .knownHostsPath = QStringLiteral("known_hosts"),
    };
}

ztermy::sftp::SftpClientFactory fakeFactory(const std::shared_ptr<FakeState> &state)
{
    return [state](ztermy::ssh::SshConnectionRequest &request, const ztermy::ssh::SshConnectionCallbacks &,
                   const std::stop_token &)
               -> std::expected<std::unique_ptr<ztermy::sftp::SftpClient>, ztermy::ssh::SshBootstrapError> {
        request.secret.clear();
        return std::unique_ptr<ztermy::sftp::SftpClient>(std::make_unique<FakeSftpClient>(state));
    };
}

class SftpSessionTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rejectsInvalidConnectionRequests();
    void suppressesStaleDirectoryResults();
    void deliversIndependentTreeDirectoryResults();
    void serializesMutatingOperations();
    void stopsOutstandingDirectoryRequestsWithoutBlockingCaller();
    void survivesRepeatedStartAndDeferredStop();
};

void SftpSessionTests::initTestCase()
{
    qRegisterMetaType<ztermy::sftp::DirectoryListingPtr>();
    qRegisterMetaType<ztermy::sftp::SftpOperationKind>();
}

void SftpSessionTests::deliversIndependentTreeDirectoryResults()
{
    const auto state = std::make_shared<FakeState>();
    ztermy::sftp::SftpSession session(fakeFactory(state));
    QSignalSpy treeSpy(&session, &ztermy::sftp::SftpSession::treeDirectoryReady);
    QSignalSpy directorySpy(&session, &ztermy::sftp::SftpSession::directoryReady);

    QVERIFY(!session.start(validRequest()));
    QTRY_VERIFY(session.running());
    session.requestTreeDirectory(11, 4, QStringLiteral("/one"));
    session.requestTreeDirectory(12, 4, QStringLiteral("/two"));
    session.requestDirectory(20, 4, QStringLiteral("/active"));

    QTRY_COMPARE(treeSpy.count(), 2);
    QTRY_COMPARE(directorySpy.count(), 1);
    QCOMPARE(treeSpy.at(0).at(2).toString(), QStringLiteral("/one"));
    QCOMPARE(treeSpy.at(1).at(2).toString(), QStringLiteral("/two"));
    QCOMPARE(directorySpy.front().at(2).toString(), QStringLiteral("/active"));
}

void SftpSessionTests::rejectsInvalidConnectionRequests()
{
    ztermy::sftp::SftpSession session(fakeFactory(std::make_shared<FakeState>()));
    auto request = validRequest();
    request.host.clear();

    QCOMPARE(session.start(std::move(request)), std::make_error_code(std::errc::invalid_argument));
    QVERIFY(!session.running());
}

void SftpSessionTests::suppressesStaleDirectoryResults()
{
    const auto state = std::make_shared<FakeState>();
    ztermy::sftp::SftpSession session(fakeFactory(state));
    QSignalSpy directorySpy(&session, &ztermy::sftp::SftpSession::directoryReady);

    QVERIFY(!session.start(validRequest()));
    QTRY_VERIFY(session.running());
    session.requestDirectory(1, 1, QStringLiteral("/slow"));
    QTRY_VERIFY(state->slowListEntered.load());

    session.requestDirectory(2, 2, QStringLiteral("/new"));
    state->releaseSlowList.store(true);
    state->available.notify_all();

    QTRY_COMPARE(directorySpy.count(), 1);
    const QList<QVariant> arguments = directorySpy.takeFirst();
    QCOMPARE(arguments.at(0).toULongLong(), quint64{2});
    QCOMPARE(arguments.at(1).toULongLong(), quint64{2});
    QCOMPARE(arguments.at(2).toString(), QStringLiteral("/new"));
    const auto entries = qvariant_cast<ztermy::sftp::DirectoryListingPtr>(arguments.at(3));
    QVERIFY(entries != nullptr);
    QCOMPARE(entries->size(), std::size_t{1});
    QCOMPARE(entries->front().remotePath, std::string("/new/entry"));
}

void SftpSessionTests::serializesMutatingOperations()
{
    const auto state = std::make_shared<FakeState>();
    ztermy::sftp::SftpSession session(fakeFactory(state));
    QSignalSpy successSpy(&session, &ztermy::sftp::SftpSession::operationSucceeded);

    QVERIFY(!session.start(validRequest()));
    QTRY_VERIFY(session.running());
    session.requestCreateDirectory(10, QStringLiteral("/work"));
    session.requestCreateFile(11, QStringLiteral("/work/readme.txt"));
    session.requestRenameEntry(12, QStringLiteral("/work"), QStringLiteral("/renamed"));
    session.requestRemoveEntry(13, QStringLiteral("/renamed"), true);

    QTRY_COMPARE(successSpy.count(), 4);
    std::scoped_lock lock(state->mutex);
    QCOMPARE(state->created, std::vector<std::string>{"/work"});
    QCOMPARE(state->createdFiles, std::vector<std::string>{"/work/readme.txt"});
    QCOMPARE(state->renamed, (std::vector<std::pair<std::string, std::string>>{{"/work", "/renamed"}}));
    QCOMPARE(state->removed, (std::vector<std::pair<std::string, bool>>{{"/renamed", true}}));
}

void SftpSessionTests::stopsOutstandingDirectoryRequestsWithoutBlockingCaller()
{
    const auto state = std::make_shared<FakeState>();
    ztermy::sftp::SftpSession session(fakeFactory(state));
    QSignalSpy finishedSpy(&session, &ztermy::sftp::SftpSession::workerFinishedChanged);

    QVERIFY(!session.start(validRequest()));
    QTRY_VERIFY(session.running());
    session.requestDirectory(1, 1, QStringLiteral("/slow"));
    QTRY_VERIFY(state->slowListEntered.load());

    session.requestStop();
    QTRY_COMPARE(finishedSpy.count(), 1);
    QVERIFY(session.workerFinished());
    QTRY_VERIFY(!session.running());
}

void SftpSessionTests::survivesRepeatedStartAndDeferredStop()
{
    for (int iteration = 0; iteration < 25; ++iteration)
    {
        const auto state = std::make_shared<FakeState>();
        ztermy::sftp::SftpSession session(fakeFactory(state));
        QSignalSpy finishedSpy(&session, &ztermy::sftp::SftpSession::workerFinishedChanged);

        QVERIFY(!session.start(validRequest()));
        QTRY_VERIFY(session.running());
        session.requestDirectory(1, 1, QStringLiteral("/slow"));
        QTRY_VERIFY(state->slowListEntered.load());
        session.requestStop();
        QTRY_COMPARE(finishedSpy.count(), 1);
        QVERIFY(session.workerFinished());
    }
}

} // namespace

QTEST_GUILESS_MAIN(SftpSessionTests)

#include "sftp_session_tests.moc"
