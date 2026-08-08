#pragma once

#include "application/sftp/SftpClient.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <variant>

namespace ztermy::sftp
{

enum class SftpOperationKind : std::uint8_t
{
    ListDirectory,
    CreateDirectory,
    CreateFile,
    RenameEntry,
    RemoveFile,
    RemoveDirectory,
};

using DirectoryListing = std::vector<DirectoryEntry>;
using DirectoryListingPtr = std::shared_ptr<const DirectoryListing>;

class SftpSession final : public QObject
{
    Q_OBJECT

public:
    explicit SftpSession(SftpClientFactory clientFactory = {}, QObject *parent = nullptr);
    ~SftpSession() override;

    SftpSession(const SftpSession &) = delete;
    SftpSession &operator=(const SftpSession &) = delete;

    [[nodiscard]] std::error_code start(ssh::SshConnectionRequest request);
    void requestStop() noexcept;
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool workerFinished() const noexcept;

public slots:
    void confirmHostKey(bool remember);
    void rejectHostKey();
    void requestDirectory(quint64 requestId, quint64 generation, const QString &remotePath);
    void requestTreeDirectory(quint64 requestId, quint64 generation, const QString &remotePath);
    void requestCreateDirectory(quint64 requestId, const QString &remotePath);
    void requestCreateFile(quint64 requestId, const QString &remotePath);
    void requestRenameEntry(quint64 requestId, const QString &sourcePath, const QString &destinationPath);
    void requestRemoveEntry(quint64 requestId, const QString &remotePath, bool directory);

signals:
    void runningChanged(bool running);
    void workerFinishedChanged();
    void homeDirectoryReady(const QString &remotePath);
    void sshPhaseChanged(ztermy::ssh::SshConnectionPhase phase);
    void connectionFailed(ztermy::ssh::SshFailureKind failure);
    void hostKeyConfirmationRequired(const QString &algorithm, const QString &fingerprint);
    void hostKeyChanged(const QString &algorithm, const QString &fingerprint);
    void directoryReady(quint64 requestId, quint64 generation, const QString &remotePath,
                        ztermy::sftp::DirectoryListingPtr entries);
    void treeDirectoryReady(quint64 requestId, quint64 generation, const QString &remotePath,
                            ztermy::sftp::DirectoryListingPtr entries);
    void treeDirectoryFailed(quint64 requestId, quint64 generation, const QString &remotePath,
                             ztermy::ssh::SshTransportErrorKind error);
    void operationSucceeded(quint64 requestId, ztermy::sftp::SftpOperationKind operation);
    void operationFailed(quint64 requestId, ztermy::sftp::SftpOperationKind operation,
                         ztermy::ssh::SshTransportErrorKind error);

private slots:
    void deliverRunning(bool running);
    void deliverWorkerFinished();
    void deliverHomeDirectory(const QString &remotePath);
    void deliverPhase(ztermy::ssh::SshConnectionPhase phase);
    void deliverConnectionFailure(ztermy::ssh::SshFailureKind failure);
    void deliverHostKeyConfirmation(const QString &algorithm, const QString &fingerprint);
    void deliverHostKeyChange(const QString &algorithm, const QString &fingerprint);
    void deliverDirectory(quint64 requestId, quint64 generation, const QString &remotePath,
                          ztermy::sftp::DirectoryListingPtr entries);
    void deliverTreeDirectory(quint64 requestId, quint64 generation, const QString &remotePath,
                              ztermy::sftp::DirectoryListingPtr entries);
    void deliverTreeDirectoryFailure(quint64 requestId, quint64 generation, const QString &remotePath,
                                     ztermy::ssh::SshTransportErrorKind error);
    void deliverOperationSucceeded(quint64 requestId, ztermy::sftp::SftpOperationKind operation);
    void deliverOperationFailed(quint64 requestId, ztermy::sftp::SftpOperationKind operation,
                                ztermy::ssh::SshTransportErrorKind error);

private:
    struct ListDirectoryCommand final
    {
        quint64 requestId = 0;
        quint64 generation = 0;
        std::string remotePath;
    };
    struct CreateDirectoryCommand final
    {
        quint64 requestId = 0;
        std::string remotePath;
    };
    struct ListTreeDirectoryCommand final
    {
        quint64 requestId = 0;
        quint64 generation = 0;
        std::string remotePath;
    };
    struct CreateFileCommand final
    {
        quint64 requestId = 0;
        std::string remotePath;
    };
    struct RenameEntryCommand final
    {
        quint64 requestId = 0;
        std::string sourcePath;
        std::string destinationPath;
    };
    struct RemoveEntryCommand final
    {
        quint64 requestId = 0;
        std::string remotePath;
        bool directory = false;
    };
    using Command = std::variant<ListDirectoryCommand, ListTreeDirectoryCommand, CreateDirectoryCommand,
                                 CreateFileCommand, RenameEntryCommand, RemoveEntryCommand>;

    void run(ssh::SshConnectionRequest &request, const std::stop_token &stopToken);
    void processCommand(SftpClient &client, Command command, const std::stop_token &stopToken);
    enum class EnqueueResult : std::uint8_t
    {
        Accepted,
        Stopped,
        LimitReached,
    };

    [[nodiscard]] EnqueueResult enqueue(Command command);
    [[nodiscard]] bool isCurrentDirectoryRequest(quint64 requestId, quint64 generation) const noexcept;
    void postRunning(bool running);
    void postWorkerFinished();
    void postHomeDirectory(const QString &remotePath);
    void postPhase(ssh::SshConnectionPhase phase);
    void postConnectionFailure(ssh::SshFailureKind failure);
    void postHostKeyConfirmation(const QString &algorithm, const QString &fingerprint);
    void postHostKeyChange(const QString &algorithm, const QString &fingerprint);
    void postDirectory(quint64 requestId, quint64 generation, const QString &remotePath, DirectoryListingPtr entries);
    void postTreeDirectory(quint64 requestId, quint64 generation, const QString &remotePath,
                           DirectoryListingPtr entries);
    void postTreeDirectoryFailure(quint64 requestId, quint64 generation, const QString &remotePath,
                                  ssh::SshTransportErrorKind error);
    void postOperationSucceeded(quint64 requestId, SftpOperationKind operation);
    void postOperationFailed(quint64 requestId, SftpOperationKind operation, ssh::SshTransportErrorKind error);

    SftpClientFactory m_clientFactory;
    std::jthread m_worker;
    std::atomic_bool m_running = false;
    std::atomic_bool m_workerFinished = true;

    mutable std::mutex m_commandMutex;
    std::condition_variable_any m_commandAvailable;
    std::deque<Command> m_commands;
    static constexpr std::size_t maximumQueuedTreeCommands = 128;
    std::atomic_bool m_acceptingCommands = false;
    quint64 m_latestDirectoryRequestId = 0;
    quint64 m_latestDirectoryGeneration = 0;

    std::mutex m_hostKeyMutex;
    std::condition_variable_any m_hostKeyAvailable;
    std::optional<ssh::UnknownHostKeyDecision> m_hostKeyDecision;
    bool m_awaitingHostKey = false;
};

} // namespace ztermy::sftp

Q_DECLARE_METATYPE(ztermy::sftp::DirectoryListingPtr)
Q_DECLARE_METATYPE(ztermy::sftp::SftpOperationKind)
