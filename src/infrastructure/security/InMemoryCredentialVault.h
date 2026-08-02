#pragma once

#include "core/security/CredentialVault.h"

#include <map>
#include <mutex>
#include <utility>

namespace ztermy::security
{

class InMemoryCredentialVault final : public CredentialVault
{
public:
    InMemoryCredentialVault() = default;
    ~InMemoryCredentialVault() override;

    [[nodiscard]] std::string_view backendId() const noexcept override;
    [[nodiscard]] bool persistent() const noexcept override;
    [[nodiscard]] std::expected<void, CredentialVaultError> store(const CredentialKey &key,
                                                                  SensitiveByteArray secret) override;
    [[nodiscard]] std::expected<SensitiveByteArray, CredentialVaultError> read(const CredentialKey &key) const override;
    [[nodiscard]] std::expected<void, CredentialVaultError> remove(const CredentialKey &key) override;
    [[nodiscard]] std::expected<std::vector<CredentialKey>, CredentialVaultError> listKeys() const override;

private:
    using MapKey = std::pair<std::string, CredentialKind>;
    mutable std::mutex m_mutex;
    std::map<MapKey, QByteArray> m_secrets;
};

} // namespace ztermy::security
