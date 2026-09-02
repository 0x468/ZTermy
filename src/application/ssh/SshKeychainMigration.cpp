#include "application/ssh/SshKeychainMigration.h"

#include "domain/ssh/SshKeychain.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace
{

[[nodiscard]] ztermy::ssh::SshIdentityAuthentication
identityAuthentication(const ztermy::ssh::SshAuthenticationMethod authentication) noexcept
{
    switch (authentication)
    {
        case ztermy::ssh::SshAuthenticationMethod::Password:
            return ztermy::ssh::SshIdentityAuthentication::Password;
        case ztermy::ssh::SshAuthenticationMethod::PrivateKey:
            return ztermy::ssh::SshIdentityAuthentication::PrivateKey;
        case ztermy::ssh::SshAuthenticationMethod::Agent:
            return ztermy::ssh::SshIdentityAuthentication::Agent;
    }
    return ztermy::ssh::SshIdentityAuthentication::Password;
}

} // namespace

namespace ztermy::ssh
{

std::expected<SshKeychainMigrationPlan, SshKeychainMigrationError>
planLegacyKeychainMigration(const std::vector<SshProfile> &profiles, const SshKeychainCatalog &catalog)
{
    SshKeychainMigrationPlan result{.profiles = profiles, .catalog = catalog};
    for (SshProfile &profile : result.profiles)
    {
        if (profile.identityReference)
        {
            continue;
        }
        const std::string recordId = profile.id;
        std::optional<std::string> keyId;
        if (profile.authentication == SshAuthenticationMethod::PrivateKey)
        {
            const SshKeyRecord expectedKey{
                .id = recordId,
                .label = profile.name,
                .type = SshKeyType::Unknown,
                .kind = SshKeyKind::Key,
                .source = SshKeySource::Reference,
                .privateKeyPath = profile.privateKeyPath,
                .createdUtcMs = profile.lastConnectedUtcMs.value_or(0),
            };
            const auto existingKey = std::ranges::find(result.catalog.keys, recordId, &SshKeyRecord::id);
            if (existingKey == result.catalog.keys.end() && result.catalog.legacyProfilesMigrated)
            {
                continue;
            }
            if (existingKey == result.catalog.keys.end())
            {
                result.catalog.keys.push_back(expectedKey);
            }
            else if (*existingKey != expectedKey)
            {
                return std::unexpected(SshKeychainMigrationError::ConflictingKey);
            }
            keyId = recordId;
        }
        const SshIdentity expectedIdentity{
            .id = recordId,
            .label = profile.name,
            .username = profile.username,
            .authentication = identityAuthentication(profile.authentication),
            .keyId = std::move(keyId),
            .credentialRequired =
                profile.authentication == SshAuthenticationMethod::Password || profile.privateKeyPassphraseRequired,
            .credentialReference = profile.credentialReference,
            .createdUtcMs = profile.lastConnectedUtcMs.value_or(0),
        };
        const auto existingIdentity = std::ranges::find(result.catalog.identities, recordId, &SshIdentity::id);
        if (existingIdentity == result.catalog.identities.end() && result.catalog.legacyProfilesMigrated)
        {
            continue;
        }
        if (existingIdentity == result.catalog.identities.end())
        {
            result.catalog.identities.push_back(expectedIdentity);
        }
        else if (*existingIdentity != expectedIdentity)
        {
            return std::unexpected(SshKeychainMigrationError::ConflictingIdentity);
        }
        profile.identityReference = recordId;
        result.changed = true;
    }

    if (!result.catalog.legacyProfilesMigrated)
    {
        result.catalog.legacyProfilesMigrated = true;
        result.changed = true;
    }

    if (std::ranges::any_of(result.profiles,
                            [](const SshProfile &profile) {
                                return !validSshProfile(profile);
                            })
        || std::ranges::any_of(result.catalog.keys,
                               [](const SshKeyRecord &record) {
                                   return !validSshKeyRecord(record);
                               })
        || std::ranges::any_of(result.catalog.identities, [](const SshIdentity &identity) {
               return !validSshIdentity(identity);
           }))
    {
        return std::unexpected(SshKeychainMigrationError::InvalidResult);
    }
    return result;
}

} // namespace ztermy::ssh
