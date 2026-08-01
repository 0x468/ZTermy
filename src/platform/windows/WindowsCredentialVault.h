#pragma once

#include "core/security/CredentialVault.h"

#include <string_view>

namespace ztermy::security
{

class WindowsCredentialVault final : public CredentialVault
{
public:
    [[nodiscard]] std::string_view backendId() const noexcept override;
    [[nodiscard]] bool persistent() const noexcept override;
    [[nodiscard]] std::expected<void, CredentialVaultError> store(const CredentialKey &key,
                                                                  SensitiveByteArray secret) override;
    [[nodiscard]] std::expected<SensitiveByteArray, CredentialVaultError> read(const CredentialKey &key) const override;
    [[nodiscard]] std::expected<void, CredentialVaultError> remove(const CredentialKey &key) override;
    [[nodiscard]] std::expected<std::vector<CredentialKey>, CredentialVaultError> listKeys() const override;
};

} // namespace ztermy::security
