#pragma once

#include "domain/ssh/SshProfile.h"

#include <QString>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace ztermy::ssh
{

enum class SshProfileStoreError : std::uint8_t
{
    InvalidPath,
    IoError,
    InvalidFormat,
    UnsupportedVersion,
};

class SshProfileStore final
{
public:
    explicit SshProfileStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] std::expected<std::vector<SshProfile>, SshProfileStoreError> load() const;
    [[nodiscard]] std::expected<void, SshProfileStoreError> save(std::span<const SshProfile> profiles) const;

private:
    QString m_filePath;
};

} // namespace ztermy::ssh
