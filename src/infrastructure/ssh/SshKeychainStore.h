#pragma once

#include "domain/ssh/SshKeychain.h"

#include <QString>

#include <cstdint>
#include <expected>
#include <vector>

namespace ztermy::ssh
{

enum class SshKeychainStoreError : std::uint8_t
{
    InvalidPath,
    IoError,
    InvalidFormat,
    UnsupportedVersion,
};

class SshKeychainStore final
{
public:
    explicit SshKeychainStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] bool lastLoadRecoveredFromBackup() const noexcept;
    [[nodiscard]] std::expected<SshKeychainCatalog, SshKeychainStoreError> load() const;
    [[nodiscard]] std::expected<void, SshKeychainStoreError> save(const SshKeychainCatalog &catalog) const;

private:
    QString m_filePath;
    mutable bool m_lastLoadRecoveredFromBackup = false;
};

} // namespace ztermy::ssh
