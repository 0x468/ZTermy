#include "domain/ssh/SshIdentityResolver.h"

#include <algorithm>

namespace ztermy::ssh
{

std::expected<ResolvedSshIdentity, SshIdentityResolutionError> resolveSshIdentity(const SshProfile &profile,
                                                                                  const SshKeychainCatalog &catalog)
{
    if (!profile.identityReference)
    {
        return ResolvedSshIdentity{
            .username = profile.username,
            .authentication = profile.authentication,
            .privateKeyPath = profile.privateKeyPath,
            .credentialRequired =
                profile.authentication == SshAuthenticationMethod::Password || profile.privateKeyPassphraseRequired,
            .credentialReference = profile.credentialReference,
        };
    }

    const auto identity = std::ranges::find(catalog.identities, *profile.identityReference, &SshIdentity::id);
    if (identity == catalog.identities.end())
    {
        return std::unexpected(SshIdentityResolutionError::MissingIdentity);
    }
    if (!validSshIdentity(*identity))
    {
        return std::unexpected(SshIdentityResolutionError::InvalidIdentity);
    }

    ResolvedSshIdentity result{
        .username = identity->username,
        .credentialRequired = identity->credentialRequired,
        .credentialReference = identity->credentialReference,
    };
    switch (identity->authentication)
    {
        case SshIdentityAuthentication::Password:
            result.authentication = SshAuthenticationMethod::Password;
            break;
        case SshIdentityAuthentication::Agent:
            result.authentication = SshAuthenticationMethod::Agent;
            break;
        case SshIdentityAuthentication::PrivateKey:
        case SshIdentityAuthentication::Certificate:
        {
            const auto key = std::ranges::find(catalog.keys, *identity->keyId, &SshKeyRecord::id);
            if (key == catalog.keys.end())
            {
                return std::unexpected(SshIdentityResolutionError::MissingKey);
            }
            const bool certificate = identity->authentication == SshIdentityAuthentication::Certificate;
            if (!validSshKeyRecord(*key)
                || (certificate ? key->kind != SshKeyKind::Certificate : key->kind != SshKeyKind::Key))
            {
                return std::unexpected(SshIdentityResolutionError::InvalidIdentity);
            }
            result.authentication = SshAuthenticationMethod::PrivateKey;
            result.privateKeyPath = key->privateKeyPath;
            result.publicKeyPath = certificate ? key->certificatePath : key->publicKeyPath;
            break;
        }
    }
    return result;
}

} // namespace ztermy::ssh
