#pragma once

#include "application/sftp/SftpClient.h"
#include "application/sftp/TransferExecutor.h"
#include "domain/sftp/TransferQueue.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ztermy::sftp
{

enum class TransferCredentialError : std::uint8_t
{
    Locked,
    Unavailable,
    Cancelled,
};

using TransferRequestProvider = std::function<std::expected<ssh::SshConnectionRequest, TransferCredentialError>()>;
using TransferRecoveryRequestProviderFactory = std::function<TransferRequestProvider(const std::string &endpointId)>;
using TransferTasksPtr = std::shared_ptr<const std::vector<TransferTask>>;
using FileConflictPtr = std::shared_ptr<const FileConflict>;

class TransferManager final : public QObject
{
    Q_OBJECT

public:
    explicit TransferManager(std::size_t concurrencyLimit = 2, SftpClientFactory clientFactory = {},
                             QObject *parent = nullptr);
    ~TransferManager() override;

    TransferManager(const TransferManager &) = delete;
    TransferManager &operator=(const TransferManager &) = delete;

    [[nodiscard]] std::expected<void, TransferQueueError>
    enqueue(TransferTask task, TransferRequestProvider requestProvider, TransferExecutionOptions options = {});
    [[nodiscard]] TransferTasksPtr snapshot() const;
    void enableRecovery(QString path, TransferRecoveryRequestProviderFactory requestProviderFactory);
    void requestStop() noexcept;
    void shutdown() noexcept;

public slots:
    void pause(const QString &taskId);
    void resume(const QString &taskId);
    void cancel(const QString &taskId);
    void retry(const QString &taskId);
    void dismiss(const QString &taskId);
    void dismissFinished();
    void pauseAll();
    void resumeAll();
    void cancelAll();
    void resolveConflict(const QString &taskId, ztermy::sftp::ConflictAction action,
                         const QString &renamedDestinationPath = {});
    void confirmHostKey(const QString &taskId, bool remember);
    void rejectHostKey(const QString &taskId);

signals:
    void tasksChanged(ztermy::sftp::TransferTasksPtr tasks);
    void conflictRequired(const QString &taskId, ztermy::sftp::FileConflictPtr conflict);
    void hostKeyConfirmationRequired(const QString &taskId, const QString &endpoint, const QString &algorithm,
                                     const QString &fingerprint);
    void hostKeyChanged(const QString &taskId, const QString &endpoint, const QString &algorithm,
                        const QString &fingerprint);
    void recoveryError(const QString &errorCode);

private slots:
    void deliverCredentialError(const QString &taskId, ztermy::sftp::TransferCredentialError error);
    void deliverHostKeyConfirmation(const QString &taskId, const QString &endpoint, const QString &algorithm,
                                    const QString &fingerprint);
    void deliverHostKeyChange(const QString &taskId, const QString &endpoint, const QString &algorithm,
                              const QString &fingerprint);
    void deliverProgress(const QString &taskId, qulonglong transferredBytes, qulonglong totalBytes,
                         qulonglong bytesPerSecond);
    void deliverResult(const QString &taskId, ztermy::sftp::TransferExecutionResult result);

private:
    struct WorkSpec final
    {
        TransferRequestProvider requestProvider;
        TransferExecutionOptions options;
    };

    struct WorkerContext final
    {
        std::mutex mutex;
        std::condition_variable_any hostKeyAvailable;
        std::optional<ssh::UnknownHostKeyDecision> hostKeyDecision;
        std::atomic_bool pauseRequested = false;
        bool awaitingHostKey = false;
    };

    struct WorkerInput final
    {
        TransferTask task;
        WorkSpec spec;
    };

    struct WorkerRecord final
    {
        std::shared_ptr<WorkerContext> context;
        std::jthread thread;
    };

    struct CleanupWorkerRecord final
    {
        std::shared_ptr<std::atomic_bool> finished;
        std::jthread thread;
    };

    void schedule();
    void startWorker(const TransferTask &task, const WorkSpec &spec);
    void cleanupPartial(const TransferTask &task, const WorkSpec &spec);
    void pruneCleanupWorkers();
    void handleProgress(const std::string &taskId, std::uint64_t transferredBytes, std::uint64_t totalBytes,
                        std::uint64_t bytesPerSecond);
    void handleCredentialError(const std::string &taskId, TransferCredentialError error);
    void handleResult(const std::string &taskId, TransferExecutionResult result);
    void publishSnapshot(bool persist = true);
    void persistRecovery();
    [[nodiscard]] static QString qTaskId(std::string_view taskId);
    [[nodiscard]] static std::string taskId(const QString &taskId);

    TransferQueue m_queue;
    SftpClientFactory m_clientFactory;
    std::unordered_map<std::string, WorkSpec> m_work;
    std::unordered_map<std::string, WorkerRecord> m_workers;
    std::vector<CleanupWorkerRecord> m_cleanupWorkers;
    std::unique_ptr<class TransferRecoveryStore> m_recoveryStore;
    TransferRecoveryRequestProviderFactory m_recoveryRequestProviderFactory;
    bool m_recoveryWriteFailed = false;
    bool m_stopRequested = false;
    bool m_shutdownComplete = false;
};

} // namespace ztermy::sftp

Q_DECLARE_METATYPE(ztermy::sftp::TransferTasksPtr)
Q_DECLARE_METATYPE(ztermy::sftp::FileConflictPtr)
Q_DECLARE_METATYPE(ztermy::sftp::ConflictAction)
Q_DECLARE_METATYPE(ztermy::sftp::TransferCredentialError)
Q_DECLARE_METATYPE(ztermy::sftp::TransferExecutionResult)
