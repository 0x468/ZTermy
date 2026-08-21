#include "application/ai/AiSecretStore.h"

#include <string>
#include <utility>

namespace
{

[[nodiscard]] ztermy::security::SensitiveByteArray cloneSecret(const ztermy::security::SensitiveByteArray &secret)
{
    const std::string_view bytes = secret.view();
    return ztermy::security::SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
}

} // namespace

namespace ztermy::ai
{

AiSecretStore::AiSecretStore(security::CredentialVault &vault) noexcept : m_vault(vault) {}

std::expected<void, security::CredentialVaultError>
AiSecretStore::storeApiKey(const std::string_view credentialReference, security::SensitiveByteArray apiKey)
{
    return m_vault.store(key(credentialReference), std::move(apiKey));
}

std::expected<security::SensitiveByteArray, security::CredentialVaultError>
AiSecretStore::readApiKey(const std::string_view credentialReference) const
{
    return m_vault.read(key(credentialReference));
}

std::expected<void, security::CredentialVaultError>
AiSecretStore::removeApiKey(const std::string_view credentialReference)
{
    return m_vault.remove(key(credentialReference));
}

std::expected<void, security::CredentialVaultError>
AiSecretStore::storeOAuthTokens(const std::string_view credentialReference, AiOAuthTokens tokens)
{
    const security::CredentialKey accessKey = key(credentialReference, security::CredentialKind::AiOAuthAccessToken);
    const security::CredentialKey refreshKey = key(credentialReference, security::CredentialKind::AiOAuthRefreshToken);
    auto previousRefresh = m_vault.read(refreshKey);
    if (!previousRefresh && previousRefresh.error() != security::CredentialVaultError::NotFound)
    {
        return std::unexpected(previousRefresh.error());
    }

    if (auto stored = m_vault.store(refreshKey, std::move(tokens.refreshToken)); !stored)
    {
        return stored;
    }
    if (auto stored = m_vault.store(accessKey, std::move(tokens.accessToken)); !stored)
    {
        const auto rollback =
            previousRefresh ? m_vault.store(refreshKey, std::move(*previousRefresh)) : m_vault.remove(refreshKey);
        if (!rollback
            && !(previousRefresh.has_value() == false && rollback.error() == security::CredentialVaultError::NotFound))
        {
            return std::unexpected(security::CredentialVaultError::MigrationFailed);
        }
        return stored;
    }
    return {};
}

std::expected<AiOAuthTokens, security::CredentialVaultError>
AiSecretStore::readOAuthTokens(const std::string_view credentialReference) const
{
    auto accessToken = m_vault.read(key(credentialReference, security::CredentialKind::AiOAuthAccessToken));
    if (!accessToken)
    {
        return std::unexpected(accessToken.error());
    }
    auto refreshToken = m_vault.read(key(credentialReference, security::CredentialKind::AiOAuthRefreshToken));
    if (!refreshToken)
    {
        return std::unexpected(refreshToken.error());
    }
    return AiOAuthTokens{.accessToken = std::move(*accessToken), .refreshToken = std::move(*refreshToken)};
}

std::expected<void, security::CredentialVaultError>
AiSecretStore::removeOAuthTokens(const std::string_view credentialReference)
{
    const security::CredentialKey accessKey = key(credentialReference, security::CredentialKind::AiOAuthAccessToken);
    const security::CredentialKey refreshKey = key(credentialReference, security::CredentialKind::AiOAuthRefreshToken);
    auto previousAccess = m_vault.read(accessKey);
    if (!previousAccess && previousAccess.error() != security::CredentialVaultError::NotFound)
    {
        return std::unexpected(previousAccess.error());
    }
    if (previousAccess)
    {
        if (auto removed = m_vault.remove(accessKey); !removed)
        {
            return removed;
        }
    }
    if (auto removed = m_vault.remove(refreshKey); !removed)
    {
        if (removed.error() == security::CredentialVaultError::NotFound)
        {
            return {};
        }
        if (previousAccess)
        {
            if (auto rollback = m_vault.store(accessKey, cloneSecret(*previousAccess)); !rollback)
            {
                return std::unexpected(security::CredentialVaultError::MigrationFailed);
            }
        }
        return removed;
    }
    return {};
}

security::CredentialKey AiSecretStore::key(const std::string_view credentialReference,
                                           const security::CredentialKind kind)
{
    return security::CredentialKey{.profileId = std::string(credentialReference), .kind = kind};
}

} // namespace ztermy::ai
