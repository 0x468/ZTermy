#include "domain/ssh/SshKeychain.h"

#include <cstddef>

namespace
{

constexpr std::size_t maximumIdLength = 128;
constexpr std::size_t maximumLabelLength = 256;
constexpr std::size_t maximumUsernameLength = 256;
constexpr std::size_t maximumPathLength = 4096;

[[nodiscard]] bool bounded(const std::string &value, const std::size_t maximum, const bool requireValue) noexcept
{
    return (!requireValue || !value.empty()) && value.size() <= maximum && value.find('\0') == std::string::npos;
}

[[nodiscard]] bool validReference(const std::optional<std::string> &value) noexcept
{
    return !value || bounded(*value, maximumIdLength, true);
}

} // namespace

namespace ztermy::ssh
{

bool validSshKeyRecord(const SshKeyRecord &record) noexcept
{
    if (!bounded(record.id, maximumIdLength, true) || !bounded(record.label, maximumLabelLength, true)
        || !bounded(record.privateKeyPath, maximumPathLength, true)
        || !bounded(record.publicKeyPath, maximumPathLength, false)
        || !bounded(record.certificatePath, maximumPathLength, false) || record.createdUtcMs < 0)
    {
        return false;
    }
    return record.kind == SshKeyKind::Certificate ? !record.certificatePath.empty() : record.certificatePath.empty();
}

bool validSshIdentity(const SshIdentity &identity) noexcept
{
    if (!bounded(identity.id, maximumIdLength, true) || !bounded(identity.label, maximumLabelLength, true)
        || !bounded(identity.username, maximumUsernameLength, true) || !validReference(identity.keyId)
        || !validReference(identity.credentialReference) || identity.createdUtcMs < 0)
    {
        return false;
    }
    switch (identity.authentication)
    {
        case SshIdentityAuthentication::Password:
            return !identity.keyId && identity.credentialRequired;
        case SshIdentityAuthentication::PrivateKey:
        case SshIdentityAuthentication::Certificate:
            return identity.keyId.has_value();
        case SshIdentityAuthentication::Agent:
            return !identity.keyId && !identity.credentialRequired && !identity.credentialReference;
    }
    return false;
}

std::string_view sshKeyTypeToken(const SshKeyType type) noexcept
{
    switch (type)
    {
        case SshKeyType::Unknown:
            return "unknown";
        case SshKeyType::Ed25519:
            return "ed25519";
        case SshKeyType::Rsa:
            return "rsa";
        case SshKeyType::Ecdsa:
            return "ecdsa";
    }
    return {};
}

std::optional<SshKeyType> parseSshKeyType(const std::string_view token) noexcept
{
    if (token == "unknown")
    {
        return SshKeyType::Unknown;
    }
    if (token == "ed25519")
    {
        return SshKeyType::Ed25519;
    }
    if (token == "rsa")
    {
        return SshKeyType::Rsa;
    }
    if (token == "ecdsa")
    {
        return SshKeyType::Ecdsa;
    }
    return std::nullopt;
}

std::string_view sshKeyKindToken(const SshKeyKind kind) noexcept
{
    return kind == SshKeyKind::Certificate ? "certificate" : "key";
}

std::optional<SshKeyKind> parseSshKeyKind(const std::string_view token) noexcept
{
    if (token == "key")
    {
        return SshKeyKind::Key;
    }
    if (token == "certificate")
    {
        return SshKeyKind::Certificate;
    }
    return std::nullopt;
}

std::string_view sshKeySourceToken(const SshKeySource source) noexcept
{
    switch (source)
    {
        case SshKeySource::Generated:
            return "generated";
        case SshKeySource::Imported:
            return "imported";
        case SshKeySource::Reference:
            return "reference";
    }
    return {};
}

std::optional<SshKeySource> parseSshKeySource(const std::string_view token) noexcept
{
    if (token == "generated")
    {
        return SshKeySource::Generated;
    }
    if (token == "imported")
    {
        return SshKeySource::Imported;
    }
    if (token == "reference")
    {
        return SshKeySource::Reference;
    }
    return std::nullopt;
}

std::string_view sshIdentityAuthenticationToken(const SshIdentityAuthentication authentication) noexcept
{
    switch (authentication)
    {
        case SshIdentityAuthentication::Password:
            return "password";
        case SshIdentityAuthentication::PrivateKey:
            return "private-key";
        case SshIdentityAuthentication::Certificate:
            return "certificate";
        case SshIdentityAuthentication::Agent:
            return "agent";
    }
    return {};
}

std::optional<SshIdentityAuthentication> parseSshIdentityAuthentication(const std::string_view token) noexcept
{
    if (token == "password")
    {
        return SshIdentityAuthentication::Password;
    }
    if (token == "private-key")
    {
        return SshIdentityAuthentication::PrivateKey;
    }
    if (token == "certificate")
    {
        return SshIdentityAuthentication::Certificate;
    }
    if (token == "agent")
    {
        return SshIdentityAuthentication::Agent;
    }
    return std::nullopt;
}

} // namespace ztermy::ssh
