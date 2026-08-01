#pragma once

#include "core/security/CredentialVault.h"
#include "infrastructure/security/PortableCredentialVault.h"

#include <QString>

#include <cstddef>
#include <expected>
#include <memory>
#include <string_view>

namespace ztermy::security
{

enum class CredentialStorage : std::uint8_t
{
    System,
    Portable,
    Session,
};

struct CredentialMigrationResult final
{
    std::size_t migrated = 0;
    bool sourceCleanupComplete = true;
};

class CredentialVaultCoordinator final
{
public:
    CredentialVaultCoordinator(QString portableVaultPath, CredentialStorage initialStorage);
    CredentialVaultCoordinator(std::unique_ptr<CredentialVault> systemVault,
                               std::unique_ptr<PortableCredentialVault> portableVault,
                               std::unique_ptr<CredentialVault> sessionVault, CredentialStorage initialStorage);

    [[nodiscard]] CredentialStorage storage() const noexcept;
    [[nodiscard]] CredentialVault &active() noexcept;
    [[nodiscard]] const CredentialVault &active() const noexcept;
    [[nodiscard]] bool portableInitialized() const noexcept;
    [[nodiscard]] bool portableLocked() const noexcept;
    [[nodiscard]] std::expected<void, CredentialVaultError> initializePortable(SensitiveByteArray masterPassword);
    [[nodiscard]] std::expected<void, CredentialVaultError> unlockPortable(SensitiveByteArray masterPassword);
    [[nodiscard]] std::expected<void, CredentialVaultError>
    changePortableMasterPassword(SensitiveByteArray masterPassword);
    void lockPortable() noexcept;
    [[nodiscard]] std::expected<CredentialMigrationResult, CredentialVaultError> migrate(CredentialStorage target,
                                                                                         bool removeSource);
    [[nodiscard]] std::expected<std::size_t, CredentialVaultError> removeAll();
    [[nodiscard]] std::expected<std::size_t, CredentialVaultError> removeAll(CredentialStorage storage);
    void select(CredentialStorage storage) noexcept;

private:
    [[nodiscard]] CredentialVault &vault(CredentialStorage storage) noexcept;
    [[nodiscard]] const CredentialVault &vault(CredentialStorage storage) const noexcept;

    std::unique_ptr<CredentialVault> m_systemVault;
    std::unique_ptr<PortableCredentialVault> m_portableVault;
    std::unique_ptr<CredentialVault> m_sessionVault;
    CredentialStorage m_storage = CredentialStorage::System;
};

[[nodiscard]] std::string_view credentialStorageToken(CredentialStorage storage) noexcept;

} // namespace ztermy::security
