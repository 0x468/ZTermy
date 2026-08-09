#pragma once

#include "domain/sftp/TransferBatch.h"

#include <QString>

#include <expected>
#include <span>
#include <vector>

namespace ztermy::sftp
{

enum class TransferBatchRecoveryError : std::uint8_t
{
    Io,
    InvalidData,
    UnsupportedVersion,
};

class TransferBatchRecoveryStore final
{
public:
    explicit TransferBatchRecoveryStore(QString path);

    [[nodiscard]] std::expected<std::vector<TransferBatch>, TransferBatchRecoveryError> load() const;
    [[nodiscard]] std::expected<void, TransferBatchRecoveryError> save(std::span<const TransferBatch> batches) const;

private:
    QString m_path;
};

} // namespace ztermy::sftp
