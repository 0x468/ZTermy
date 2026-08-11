#pragma once

#include "core/security/SensitiveByteArray.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::security
{

enum class CredentialKind : std::uint8_t
{
    Password = 1,
    PrivateKeyPassphrase = 2,
    ProxyPassword = 3,
    AiApiKey = 4,
};

struct CredentialKey final
{
    std::string profileId;
    CredentialKind kind = CredentialKind::Password;

    [[nodiscard]] friend bool operator==(const CredentialKey &, const CredentialKey &) = default;
};

enum class CredentialVaultError : std::uint8_t
{
    InvalidKey,
    EmptySecret,
    SecretTooLarge,
    NotFound,
    Locked,
    Unavailable,
    AccessDenied,
    IoError,
    CorruptData,
    UnsupportedVersion,
    AuthenticationFailed,
    CryptoError,
    AlreadyInitialized,
    WeakMasterPassword,
    MigrationFailed,
};

class CredentialVault
{
public:
    virtual ~CredentialVault() = default;

    CredentialVault(const CredentialVault &) = delete;
    CredentialVault &operator=(const CredentialVault &) = delete;

    [[nodiscard]] virtual std::string_view backendId() const noexcept = 0;
    [[nodiscard]] virtual bool persistent() const noexcept = 0;
    [[nodiscard]] virtual std::expected<void, CredentialVaultError> store(const CredentialKey &key,
                                                                          SensitiveByteArray secret) = 0;
    [[nodiscard]] virtual std::expected<SensitiveByteArray, CredentialVaultError>
    read(const CredentialKey &key) const = 0;
    [[nodiscard]] virtual std::expected<void, CredentialVaultError> remove(const CredentialKey &key) = 0;
    [[nodiscard]] virtual std::expected<std::vector<CredentialKey>, CredentialVaultError> listKeys() const = 0;

protected:
    CredentialVault() = default;
};

inline constexpr std::size_t MaximumCredentialSecretSize = std::size_t{5} * 512U;

[[nodiscard]] bool validCredentialKey(const CredentialKey &key) noexcept;
[[nodiscard]] std::string_view credentialKindToken(CredentialKind kind) noexcept;

} // namespace ztermy::security
