#include "core/security/CredentialVault.h"

#include <algorithm>
#include <cctype>

namespace ztermy::security
{

bool validCredentialKey(const CredentialKey &key) noexcept
{
    if (key.profileId.empty() || key.profileId.size() > 128)
    {
        return false;
    }
    return std::ranges::all_of(key.profileId, [](const unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' || character == '_';
    });
}

std::string_view credentialKindToken(const CredentialKind kind) noexcept
{
    switch (kind)
    {
        case CredentialKind::Password:
            return "password";
        case CredentialKind::PrivateKeyPassphrase:
            return "key-passphrase";
        case CredentialKind::ProxyPassword:
            return "proxy-password";
        case CredentialKind::AiApiKey:
            return "ai-api-key";
        case CredentialKind::AiConversationKey:
            return "ai-conversation-key";
    }
    return {};
}

} // namespace ztermy::security
