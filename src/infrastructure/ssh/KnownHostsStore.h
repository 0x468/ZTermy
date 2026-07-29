#pragma once

#include "domain/ssh/SshHostKey.h"

#include <QString>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace ztermy::ssh
{

enum class KnownHostsStoreError : std::uint8_t
{
    InvalidPath,
    IoError,
    InvalidFormat,
    UnsupportedVersion,
};

class KnownHostsStore final
{
public:
    explicit KnownHostsStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] std::expected<std::vector<KnownHostEntry>, KnownHostsStoreError> load() const;
    [[nodiscard]] std::expected<void, KnownHostsStoreError> save(std::span<const KnownHostEntry> entries) const;

private:
    QString m_filePath;
};

} // namespace ztermy::ssh
