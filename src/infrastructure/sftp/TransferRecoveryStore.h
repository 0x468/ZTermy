#pragma once

#include "domain/sftp/SftpTypes.h"

#include <QString>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace ztermy::sftp
{

enum class TransferRecoveryError : std::uint8_t
{
    Io,
    InvalidData,
    UnsupportedVersion,
};

class TransferRecoveryStore final
{
public:
    explicit TransferRecoveryStore(QString path);

    [[nodiscard]] const QString &path() const noexcept;
    [[nodiscard]] std::expected<std::vector<TransferTask>, TransferRecoveryError> load() const;
    [[nodiscard]] std::expected<void, TransferRecoveryError> save(std::span<const TransferTask> tasks) const;

private:
    QString m_path;
};

} // namespace ztermy::sftp
