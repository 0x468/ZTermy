#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ssh
{

enum class SshKeyType : std::uint8_t
{
    Unknown,
    Ed25519,
    Rsa,
    Ecdsa,
};

enum class SshKeyKind : std::uint8_t
{
    Key,
    Certificate,
};

enum class SshKeySource : std::uint8_t
{
    Generated,
    Imported,
    Reference,
};

enum class SshIdentityAuthentication : std::uint8_t
{
    Password,
    PrivateKey,
    Certificate,
    Agent,
};

struct SshKeyRecord final
{
    std::string id;
    std::string label;
    SshKeyType type = SshKeyType::Unknown;
    SshKeyKind kind = SshKeyKind::Key;
    SshKeySource source = SshKeySource::Reference;
    std::string privateKeyPath;
    std::string publicKeyPath;
    std::string certificatePath;
    std::int64_t createdUtcMs = 0;

    friend bool operator==(const SshKeyRecord &, const SshKeyRecord &) = default;
};

struct SshIdentity final
{
    std::string id;
    std::string label;
    std::string username;
    SshIdentityAuthentication authentication = SshIdentityAuthentication::Password;
    std::optional<std::string> keyId;
    bool credentialRequired = true;
    std::optional<std::string> credentialReference;
    std::int64_t createdUtcMs = 0;

    friend bool operator==(const SshIdentity &, const SshIdentity &) = default;
};

struct SshKeychainCatalog final
{
    std::vector<SshKeyRecord> keys;
    std::vector<SshIdentity> identities;
    bool legacyProfilesMigrated = false;

    friend bool operator==(const SshKeychainCatalog &, const SshKeychainCatalog &) = default;
};

[[nodiscard]] bool validSshKeyRecord(const SshKeyRecord &record) noexcept;
[[nodiscard]] bool validSshIdentity(const SshIdentity &identity) noexcept;
[[nodiscard]] std::string_view sshKeyTypeToken(SshKeyType type) noexcept;
[[nodiscard]] std::optional<SshKeyType> parseSshKeyType(std::string_view token) noexcept;
[[nodiscard]] std::string_view sshKeyKindToken(SshKeyKind kind) noexcept;
[[nodiscard]] std::optional<SshKeyKind> parseSshKeyKind(std::string_view token) noexcept;
[[nodiscard]] std::string_view sshKeySourceToken(SshKeySource source) noexcept;
[[nodiscard]] std::optional<SshKeySource> parseSshKeySource(std::string_view token) noexcept;
[[nodiscard]] std::string_view sshIdentityAuthenticationToken(SshIdentityAuthentication authentication) noexcept;
[[nodiscard]] std::optional<SshIdentityAuthentication> parseSshIdentityAuthentication(std::string_view token) noexcept;

} // namespace ztermy::ssh
