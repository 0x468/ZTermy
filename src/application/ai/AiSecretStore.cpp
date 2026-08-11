#include "application/ai/AiSecretStore.h"

#include <string>
#include <utility>

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

security::CredentialKey AiSecretStore::key(const std::string_view credentialReference)
{
    return security::CredentialKey{.profileId = std::string(credentialReference),
                                   .kind = security::CredentialKind::AiApiKey};
}

} // namespace ztermy::ai
