#pragma once

#include "application/sftp/TransferManager.h"
#include "domain/sftp/TransferBatch.h"
#include "domain/sftp/TransferPlanner.h"
#include "infrastructure/sftp/TransferBatchRecoveryStore.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ztermy::sftp
{

using TransferBatchesPtr = std::shared_ptr<const std::vector<TransferBatch>>;

struct TransferBatchPlanningOutcome final
{
    std::string batchId;
    std::optional<TransferBatch> batch;
    std::vector<TransferTask> tasks;
    TransferRequestProvider requestProvider;
    std::string errorCode;
};

using TransferBatchPlanningOutcomePtr = std::shared_ptr<TransferBatchPlanningOutcome>;

enum class TransferBatchCoordinatorError : std::uint8_t
{
    InvalidRequest,
    Capacity,
    Unavailable,
};

class TransferBatchCoordinator final : public QObject
{
    Q_OBJECT

public:
    explicit TransferBatchCoordinator(TransferManager &transferManager, SftpClientFactory clientFactory = {},
                                      QObject *parent = nullptr);
    ~TransferBatchCoordinator() override;

    TransferBatchCoordinator(const TransferBatchCoordinator &) = delete;
    TransferBatchCoordinator &operator=(const TransferBatchCoordinator &) = delete;

    [[nodiscard]] std::expected<std::string, TransferBatchCoordinatorError>
    enqueue(TransferPlanRequest request, TransferRequestProvider requestProvider, std::string filenameEncoding);
    [[nodiscard]] TransferBatchesPtr snapshot() const;
    [[nodiscard]] bool ownsChildTask(std::string_view taskId) const;
    [[nodiscard]] std::optional<TransferConflictPolicy> automaticConflictPolicy(std::string_view taskId) const;
    void setConflictPolicyForChild(std::string_view taskId, TransferConflictPolicy policy, bool applyToRemaining);
    void enableRecovery(QString path);
    void requestStop() noexcept;

public slots:
    void pause(const QString &batchId);
    void resume(const QString &batchId);
    void cancel(const QString &batchId);
    void retry(const QString &batchId);
    void dismiss(const QString &batchId);
    void pauseAll();
    void resumeAll();
    void cancelAll();

signals:
    void batchesChanged(ztermy::sftp::TransferBatchesPtr batches);
    void planningCompleted(ztermy::sftp::TransferBatchPlanningOutcomePtr outcome);
    void recoveryError(const QString &errorCode);

private:
    void completePlanning(const TransferBatchPlanningOutcomePtr &outcome);
    void applyTaskSnapshot(const TransferTasksPtr &tasks);
    void publishSnapshot();
    void persistRecovery();
    [[nodiscard]] TransferBatch *findBatch(std::string_view batchId);
    [[nodiscard]] const TransferBatch *findBatch(std::string_view batchId) const;
    void forEachChild(const TransferBatch &batch, const std::function<void(const QString &)> &operation);

    TransferManager &m_transferManager;
    SftpClientFactory m_clientFactory;
    std::vector<TransferBatch> m_batches;
    std::unordered_map<std::string, std::jthread> m_planners;
    std::unique_ptr<TransferBatchRecoveryStore> m_recoveryStore;
    QTimer m_recoveryTimer;
    bool m_stopRequested = false;
    bool m_pauseAllRequested = false;
};

} // namespace ztermy::sftp

Q_DECLARE_METATYPE(ztermy::sftp::TransferBatchesPtr)
Q_DECLARE_METATYPE(ztermy::sftp::TransferBatchPlanningOutcomePtr)
