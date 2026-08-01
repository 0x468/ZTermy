#include "application/security/CredentialVaultCoordinator.h"

#include "infrastructure/security/InMemoryCredentialVault.h"
#include "platform/windows/WindowsCredentialVault.h"

#include <QByteArray>
#include <QtGlobal>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace
{

struct TargetBackup final
{
    ztermy::security::CredentialKey key;
    std::optional<ztermy::security::SensitiveByteArray> secret;
};

[[nodiscard]] bool equalSecrets(const ztermy::security::SensitiveByteArray &left,
                                const ztermy::security::SensitiveByteArray &right) noexcept
{
    return left.view().size() == right.view().size() && std::ranges::equal(left.view(), right.view());
}

} // namespace

namespace ztermy::security
{

CredentialVaultCoordinator::CredentialVaultCoordinator(QString portableVaultPath,
                                                       const CredentialStorage initialStorage)
    : CredentialVaultCoordinator(std::make_unique<WindowsCredentialVault>(),
                                 std::make_unique<PortableCredentialVault>(std::move(portableVaultPath)),
                                 std::make_unique<InMemoryCredentialVault>(), initialStorage)
{
}

CredentialVaultCoordinator::CredentialVaultCoordinator(std::unique_ptr<CredentialVault> systemVault,
                                                       std::unique_ptr<PortableCredentialVault> portableVault,
                                                       std::unique_ptr<CredentialVault> sessionVault,
                                                       const CredentialStorage initialStorage)
    : m_systemVault(std::move(systemVault)),
      m_portableVault(std::move(portableVault)),
      m_sessionVault(std::move(sessionVault)),
      m_storage(initialStorage)
{
    Q_ASSERT(m_systemVault);
    Q_ASSERT(m_portableVault);
    Q_ASSERT(m_sessionVault);
}

CredentialStorage CredentialVaultCoordinator::storage() const noexcept
{
    return m_storage;
}

CredentialVault &CredentialVaultCoordinator::active() noexcept
{
    return vault(m_storage);
}

const CredentialVault &CredentialVaultCoordinator::active() const noexcept
{
    return vault(m_storage);
}

bool CredentialVaultCoordinator::portableInitialized() const noexcept
{
    return m_portableVault->initialized();
}

bool CredentialVaultCoordinator::portableLocked() const noexcept
{
    return m_portableVault->locked();
}

std::expected<void, CredentialVaultError>
CredentialVaultCoordinator::initializePortable(SensitiveByteArray masterPassword)
{
    return m_portableVault->initialize(std::move(masterPassword));
}

std::expected<void, CredentialVaultError> CredentialVaultCoordinator::unlockPortable(SensitiveByteArray masterPassword)
{
    return m_portableVault->unlock(std::move(masterPassword));
}

std::expected<void, CredentialVaultError>
CredentialVaultCoordinator::changePortableMasterPassword(SensitiveByteArray masterPassword)
{
    return m_portableVault->changeMasterPassword(std::move(masterPassword));
}

void CredentialVaultCoordinator::lockPortable() noexcept
{
    m_portableVault->lock();
}

std::expected<CredentialMigrationResult, CredentialVaultError>
CredentialVaultCoordinator::migrate(const CredentialStorage target, const bool removeSource)
{
    if (target == m_storage)
    {
        return CredentialMigrationResult{};
    }
    if (m_storage == CredentialStorage::Portable && !portableInitialized())
    {
        m_storage = target;
        return CredentialMigrationResult{};
    }
    CredentialVault &sourceVault = active();
    CredentialVault &targetVault = vault(target);
    auto keys = sourceVault.listKeys();
    if (!keys)
    {
        return std::unexpected(keys.error());
    }

    std::vector<TargetBackup> backups;
    backups.reserve(keys->size());
    std::vector<CredentialKey> written;
    written.reserve(keys->size());
    const auto rollback = [&targetVault, &backups, &written] {
        bool complete = true;
        for (auto iterator = written.rbegin(); iterator != written.rend(); ++iterator)
        {
            const auto backup = std::ranges::find_if(backups, [&iterator](const TargetBackup &candidate) {
                return candidate.key == *iterator;
            });
            if (backup != backups.end() && backup->secret)
            {
                const std::string_view bytes = backup->secret->view();
                const auto restored = targetVault.store(
                    backup->key, SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()))));
                complete = complete && restored.has_value();
            }
            else
            {
                const auto removed = targetVault.remove(*iterator);
                complete = complete && (removed.has_value() || removed.error() == CredentialVaultError::NotFound);
            }
        }
        return complete;
    };

    for (const CredentialKey &key : *keys)
    {
        auto sourceSecret = sourceVault.read(key);
        if (!sourceSecret)
        {
            const CredentialVaultError error = sourceSecret.error();
            return std::unexpected(rollback() ? error : CredentialVaultError::MigrationFailed);
        }
        auto previous = targetVault.read(key);
        if (!previous && previous.error() != CredentialVaultError::NotFound)
        {
            const CredentialVaultError error = previous.error();
            return std::unexpected(rollback() ? error : CredentialVaultError::MigrationFailed);
        }
        backups.push_back(
            TargetBackup{.key = key, .secret = previous ? std::optional{std::move(*previous)} : std::nullopt});
        const std::string_view bytes = sourceSecret->view();
        auto stored =
            targetVault.store(key, SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()))));
        if (!stored)
        {
            const CredentialVaultError error = stored.error();
            return std::unexpected(rollback() ? error : CredentialVaultError::MigrationFailed);
        }
        written.push_back(key);
        auto verified = targetVault.read(key);
        if (!verified || !equalSecrets(*sourceSecret, *verified))
        {
            if (!rollback())
            {
                return std::unexpected(CredentialVaultError::MigrationFailed);
            }
            return std::unexpected(CredentialVaultError::MigrationFailed);
        }
    }

    m_storage = target;
    CredentialMigrationResult result{.migrated = keys->size()};
    if (removeSource)
    {
        for (const CredentialKey &key : *keys)
        {
            const auto removed = sourceVault.remove(key);
            if (!removed && removed.error() != CredentialVaultError::NotFound)
            {
                result.sourceCleanupComplete = false;
            }
        }
    }
    return result;
}

std::expected<std::size_t, CredentialVaultError> CredentialVaultCoordinator::removeAll()
{
    return removeAll(m_storage);
}

std::expected<std::size_t, CredentialVaultError> CredentialVaultCoordinator::removeAll(const CredentialStorage storage)
{
    if (storage == CredentialStorage::Portable && !portableInitialized())
    {
        return std::size_t{0};
    }
    CredentialVault &selectedVault = vault(storage);
    auto keys = selectedVault.listKeys();
    if (!keys)
    {
        return std::unexpected(keys.error());
    }

    std::vector<TargetBackup> backups;
    backups.reserve(keys->size());
    for (const CredentialKey &key : *keys)
    {
        auto secret = selectedVault.read(key);
        if (!secret)
        {
            return std::unexpected(secret.error());
        }
        backups.push_back(TargetBackup{.key = key, .secret = std::optional{std::move(*secret)}});
    }

    std::vector<CredentialKey> removedKeys;
    removedKeys.reserve(keys->size());
    const auto rollback = [&selectedVault, &backups, &removedKeys] {
        bool complete = true;
        for (auto iterator = removedKeys.rbegin(); iterator != removedKeys.rend(); ++iterator)
        {
            const auto backup = std::ranges::find_if(backups, [&iterator](const TargetBackup &candidate) {
                return candidate.key == *iterator;
            });
            if (backup == backups.end() || !backup->secret)
            {
                complete = false;
                continue;
            }
            const std::string_view bytes = backup->secret->view();
            const auto restored = selectedVault.store(
                backup->key, SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()))));
            complete = complete && restored.has_value();
        }
        return complete;
    };

    std::size_t removedCount = 0;
    for (const CredentialKey &key : *keys)
    {
        const auto removed = selectedVault.remove(key);
        if (!removed && removed.error() != CredentialVaultError::NotFound)
        {
            const CredentialVaultError error = removed.error();
            return std::unexpected(rollback() ? error : CredentialVaultError::MigrationFailed);
        }
        removedKeys.push_back(key);
        if (removed)
        {
            ++removedCount;
        }
    }
    return removedCount;
}

void CredentialVaultCoordinator::select(const CredentialStorage storage) noexcept
{
    m_storage = storage;
}

CredentialVault &CredentialVaultCoordinator::vault(const CredentialStorage storage) noexcept
{
    switch (storage)
    {
        case CredentialStorage::Portable:
            return *m_portableVault;
        case CredentialStorage::Session:
            return *m_sessionVault;
        case CredentialStorage::System:
        default:
            return *m_systemVault;
    }
}

const CredentialVault &CredentialVaultCoordinator::vault(const CredentialStorage storage) const noexcept
{
    return const_cast<CredentialVaultCoordinator *>(this)->vault(storage);
}

std::string_view credentialStorageToken(const CredentialStorage storage) noexcept
{
    switch (storage)
    {
        case CredentialStorage::Portable:
            return "portable";
        case CredentialStorage::Session:
            return "session";
        case CredentialStorage::System:
        default:
            return "system";
    }
}

} // namespace ztermy::security
