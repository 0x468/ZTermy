#include "platform/windows/WindowsCredentialVault.h"

#include <Windows.h>
#include <wincred.h>

#include <QByteArray>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace
{

constexpr std::wstring_view TargetPrefix = L"ztermy:ssh:";
constexpr std::wstring_view PasswordSuffix = L":password";
constexpr std::wstring_view PassphraseSuffix = L":key-passphrase";
constexpr std::wstring_view ProxyPasswordSuffix = L":proxy-password";
constexpr std::wstring_view AiApiKeySuffix = L":ai-api-key";
constexpr std::wstring_view AiConversationKeySuffix = L":ai-conversation-key";
constexpr std::wstring_view AiOAuthAccessTokenSuffix = L":ai-oauth-access-token";
constexpr std::wstring_view AiOAuthRefreshTokenSuffix = L":ai-oauth-refresh-token";

struct CredentialDeleter final
{
    void operator()(CREDENTIALW *credential) const noexcept
    {
        if (credential != nullptr)
        {
            if (credential->CredentialBlob != nullptr && credential->CredentialBlobSize > 0)
            {
                SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
            }
            CredFree(credential);
        }
    }
};

using CredentialPointer = std::unique_ptr<CREDENTIALW, CredentialDeleter>;

[[nodiscard]] ztermy::security::CredentialVaultError mapWindowsError(const DWORD error) noexcept
{
    switch (error)
    {
        case ERROR_NOT_FOUND:
            return ztermy::security::CredentialVaultError::NotFound;
        case ERROR_ACCESS_DENIED:
            return ztermy::security::CredentialVaultError::AccessDenied;
        case ERROR_NO_SUCH_LOGON_SESSION:
            return ztermy::security::CredentialVaultError::Unavailable;
        case ERROR_INVALID_PARAMETER:
        case ERROR_BAD_USERNAME:
            return ztermy::security::CredentialVaultError::InvalidKey;
        default:
            return ztermy::security::CredentialVaultError::IoError;
    }
}

[[nodiscard]] std::wstring targetName(const ztermy::security::CredentialKey &key)
{
    std::wstring target(TargetPrefix);
    target.reserve(TargetPrefix.size() + key.profileId.size() + ProxyPasswordSuffix.size());
    for (const char character : key.profileId)
    {
        target.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    switch (key.kind)
    {
        case ztermy::security::CredentialKind::Password:
            target.append(PasswordSuffix);
            break;
        case ztermy::security::CredentialKind::PrivateKeyPassphrase:
            target.append(PassphraseSuffix);
            break;
        case ztermy::security::CredentialKind::ProxyPassword:
            target.append(ProxyPasswordSuffix);
            break;
        case ztermy::security::CredentialKind::AiApiKey:
            target.append(AiApiKeySuffix);
            break;
        case ztermy::security::CredentialKind::AiConversationKey:
            target.append(AiConversationKeySuffix);
            break;
        case ztermy::security::CredentialKind::AiOAuthAccessToken:
            target.append(AiOAuthAccessTokenSuffix);
            break;
        case ztermy::security::CredentialKind::AiOAuthRefreshToken:
            target.append(AiOAuthRefreshTokenSuffix);
            break;
    }
    return target;
}

[[nodiscard]] std::optional<ztermy::security::CredentialKey> parseTarget(const std::wstring_view target)
{
    if (!target.starts_with(TargetPrefix))
    {
        return std::nullopt;
    }
    ztermy::security::CredentialKind kind;
    std::wstring_view profile;
    if (target.ends_with(PasswordSuffix))
    {
        kind = ztermy::security::CredentialKind::Password;
        profile = target.substr(TargetPrefix.size(), target.size() - TargetPrefix.size() - PasswordSuffix.size());
    }
    else if (target.ends_with(PassphraseSuffix))
    {
        kind = ztermy::security::CredentialKind::PrivateKeyPassphrase;
        profile = target.substr(TargetPrefix.size(), target.size() - TargetPrefix.size() - PassphraseSuffix.size());
    }
    else if (target.ends_with(ProxyPasswordSuffix))
    {
        kind = ztermy::security::CredentialKind::ProxyPassword;
        profile = target.substr(TargetPrefix.size(), target.size() - TargetPrefix.size() - ProxyPasswordSuffix.size());
    }
    else if (target.ends_with(AiApiKeySuffix))
    {
        kind = ztermy::security::CredentialKind::AiApiKey;
        profile = target.substr(TargetPrefix.size(), target.size() - TargetPrefix.size() - AiApiKeySuffix.size());
    }
    else if (target.ends_with(AiConversationKeySuffix))
    {
        kind = ztermy::security::CredentialKind::AiConversationKey;
        profile =
            target.substr(TargetPrefix.size(), target.size() - TargetPrefix.size() - AiConversationKeySuffix.size());
    }
    else if (target.ends_with(AiOAuthAccessTokenSuffix))
    {
        kind = ztermy::security::CredentialKind::AiOAuthAccessToken;
        profile =
            target.substr(TargetPrefix.size(), target.size() - TargetPrefix.size() - AiOAuthAccessTokenSuffix.size());
    }
    else if (target.ends_with(AiOAuthRefreshTokenSuffix))
    {
        kind = ztermy::security::CredentialKind::AiOAuthRefreshToken;
        profile =
            target.substr(TargetPrefix.size(), target.size() - TargetPrefix.size() - AiOAuthRefreshTokenSuffix.size());
    }
    else
    {
        return std::nullopt;
    }

    std::string profileId;
    profileId.reserve(profile.size());
    for (const wchar_t character : profile)
    {
        if (character < 0 || character > 0x7f)
        {
            return std::nullopt;
        }
        profileId.push_back(static_cast<char>(character));
    }
    ztermy::security::CredentialKey key{.profileId = std::move(profileId), .kind = kind};
    return ztermy::security::validCredentialKey(key) ? std::optional{std::move(key)} : std::nullopt;
}

} // namespace

namespace ztermy::security
{

std::string_view WindowsCredentialVault::backendId() const noexcept
{
    return "system";
}

bool WindowsCredentialVault::persistent() const noexcept
{
    return true;
}

std::expected<void, CredentialVaultError> WindowsCredentialVault::store(const CredentialKey &key,
                                                                        SensitiveByteArray secret)
{
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

    std::wstring target = targetName(key);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(bytes.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (CredWriteW(&credential, 0) == FALSE)
    {
        return std::unexpected(mapWindowsError(GetLastError()));
    }
    return {};
}

std::expected<SensitiveByteArray, CredentialVaultError> WindowsCredentialVault::read(const CredentialKey &key) const
{
    if (!validCredentialKey(key))
    {
        return std::unexpected(CredentialVaultError::InvalidKey);
    }
    const std::wstring target = targetName(key);
    PCREDENTIALW rawCredential = nullptr;
    if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &rawCredential) == FALSE)
    {
        return std::unexpected(mapWindowsError(GetLastError()));
    }
    CredentialPointer credential(rawCredential);
    if (credential->CredentialBlob == nullptr || credential->CredentialBlobSize == 0
        || credential->CredentialBlobSize > MaximumCredentialSecretSize)
    {
        return std::unexpected(CredentialVaultError::CorruptData);
    }
    QByteArray secret(reinterpret_cast<const char *>(credential->CredentialBlob),
                      static_cast<qsizetype>(credential->CredentialBlobSize));
    return SensitiveByteArray(std::move(secret));
}

std::expected<void, CredentialVaultError> WindowsCredentialVault::remove(const CredentialKey &key)
{
    if (!validCredentialKey(key))
    {
        return std::unexpected(CredentialVaultError::InvalidKey);
    }
    const std::wstring target = targetName(key);
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == FALSE)
    {
        return std::unexpected(mapWindowsError(GetLastError()));
    }
    return {};
}

std::expected<std::vector<CredentialKey>, CredentialVaultError> WindowsCredentialVault::listKeys() const
{
    std::wstring filter(TargetPrefix);
    filter.push_back(L'*');
    DWORD count = 0;
    PCREDENTIALW *rawCredentials = nullptr;
    if (CredEnumerateW(filter.c_str(), 0, &count, &rawCredentials) == FALSE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_NOT_FOUND)
        {
            return std::vector<CredentialKey>{};
        }
        return std::unexpected(mapWindowsError(error));
    }

    std::unique_ptr<void, decltype(&CredFree)> credentials(static_cast<void *>(rawCredentials), &CredFree);
    std::vector<CredentialKey> keys;
    keys.reserve(count);
    for (DWORD index = 0; index < count; ++index)
    {
        const CREDENTIALW *credential = rawCredentials[index];
        if (credential == nullptr || credential->TargetName == nullptr || credential->Type != CRED_TYPE_GENERIC)
        {
            continue;
        }
        if (auto key = parseTarget(credential->TargetName))
        {
            keys.push_back(std::move(*key));
        }
    }
    std::ranges::sort(keys, [](const CredentialKey &left, const CredentialKey &right) {
        return left.profileId < right.profileId || (left.profileId == right.profileId && left.kind < right.kind);
    });
    return keys;
}

} // namespace ztermy::security
