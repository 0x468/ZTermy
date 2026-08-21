#pragma once

#include "core/security/CredentialVault.h"

#include <expected>
#include <string_view>

namespace ztermy::ai
{

struct AiOAuthTokens final
{
    security::SensitiveByteArray accessToken;
    security::SensitiveByteArray refreshToken;
};

class AiSecretStore final
{
public:
    explicit AiSecretStore(security::CredentialVault &vault) noexcept;

    [[nodiscard]] std::expected<void, security::CredentialVaultError> storeApiKey(std::string_view credentialReference,
                                                                                  security::SensitiveByteArray apiKey);
    [[nodiscard]] std::expected<security::SensitiveByteArray, security::CredentialVaultError>
    readApiKey(std::string_view credentialReference) const;
    [[nodiscard]] std::expected<void, security::CredentialVaultError>
    removeApiKey(std::string_view credentialReference);
    [[nodiscard]] std::expected<void, security::CredentialVaultError>
    storeOAuthTokens(std::string_view credentialReference, AiOAuthTokens tokens);
    [[nodiscard]] std::expected<AiOAuthTokens, security::CredentialVaultError>
    readOAuthTokens(std::string_view credentialReference) const;
    [[nodiscard]] std::expected<void, security::CredentialVaultError>
    removeOAuthTokens(std::string_view credentialReference);

private:
    [[nodiscard]] static security::CredentialKey
    key(std::string_view credentialReference, security::CredentialKind kind = security::CredentialKind::AiApiKey);

    security::CredentialVault &m_vault;
};

} // namespace ztermy::ai
