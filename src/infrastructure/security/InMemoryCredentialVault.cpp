#include "infrastructure/security/InMemoryCredentialVault.h"

#include <QByteArray>

#include <string_view>

namespace
{

void clearBytes(QByteArray &bytes) noexcept
{
    volatile char *data = bytes.data();
    for (qsizetype index = 0; index < bytes.size(); ++index)
    {
        data[index] = '\0';
    }
    bytes.clear();
    bytes.squeeze();
}

} // namespace

namespace ztermy::security
{

InMemoryCredentialVault::~InMemoryCredentialVault()
{
    const std::scoped_lock lock(m_mutex);
    for (auto &[key, secret] : m_secrets)
    {
        static_cast<void>(key);
        clearBytes(secret);
    }
}

std::string_view InMemoryCredentialVault::backendId() const noexcept
{
    return "session";
}

bool InMemoryCredentialVault::persistent() const noexcept
{
    return false;
}

std::expected<void, CredentialVaultError> InMemoryCredentialVault::store(const CredentialKey &key,
                                                                         SensitiveByteArray secret)
{
    const std::scoped_lock lock(m_mutex);
    if (!validCredentialKey(key))
    {
        return std::unexpected(CredentialVaultError::InvalidKey);
    }
    const std::string_view bytes = secret.view();
    if (bytes.empty())
    {
        return std::unexpected(CredentialVaultError::EmptySecret);
    }
    if (bytes.size() > MaximumCredentialSecretSize)
    {
        return std::unexpected(CredentialVaultError::SecretTooLarge);
    }

    QByteArray replacement(bytes.data(), static_cast<qsizetype>(bytes.size()));
    const MapKey mapKey{key.profileId, key.kind};
    if (const auto existing = m_secrets.find(mapKey); existing != m_secrets.end())
    {
        clearBytes(existing->second);
        existing->second = std::move(replacement);
    }
    else
    {
        m_secrets.emplace(mapKey, std::move(replacement));
    }
    return {};
}

std::expected<SensitiveByteArray, CredentialVaultError> InMemoryCredentialVault::read(const CredentialKey &key) const
{
    const std::scoped_lock lock(m_mutex);
    if (!validCredentialKey(key))
    {
        return std::unexpected(CredentialVaultError::InvalidKey);
    }
    const auto existing = m_secrets.find(MapKey{key.profileId, key.kind});
    if (existing == m_secrets.end())
    {
        return std::unexpected(CredentialVaultError::NotFound);
    }
    return SensitiveByteArray(QByteArray(existing->second.constData(), existing->second.size()));
}

std::expected<void, CredentialVaultError> InMemoryCredentialVault::remove(const CredentialKey &key)
{
    const std::scoped_lock lock(m_mutex);
    if (!validCredentialKey(key))
    {
        return std::unexpected(CredentialVaultError::InvalidKey);
    }
    const auto existing = m_secrets.find(MapKey{key.profileId, key.kind});
    if (existing == m_secrets.end())
    {
        return std::unexpected(CredentialVaultError::NotFound);
    }
    clearBytes(existing->second);
    m_secrets.erase(existing);
    return {};
}

std::expected<std::vector<CredentialKey>, CredentialVaultError> InMemoryCredentialVault::listKeys() const
{
    const std::scoped_lock lock(m_mutex);
    std::vector<CredentialKey> keys;
    keys.reserve(m_secrets.size());
    for (const auto &[key, secret] : m_secrets)
    {
        static_cast<void>(secret);
        keys.push_back(CredentialKey{.profileId = key.first, .kind = key.second});
    }
    return keys;
}

} // namespace ztermy::security
