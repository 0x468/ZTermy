#include "application/ssh/SshKeychainMigration.h"

#include "domain/ssh/SshKeychain.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
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

[[nodiscard]] std::string normalizedPath(std::string value)
{
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return character == '\\' ? '/' : static_cast<char>(std::tolower(character));
    });
    while (value.size() > 1 && value.back() == '/')
    {
        value.pop_back();
    }
    return value;
}

[[nodiscard]] std::string keyMaterialIdentity(const ztermy::ssh::SshKeyRecord &record)
{
    return std::to_string(static_cast<int>(record.kind)) + '|' + normalizedPath(record.privateKeyPath) + '|'
           + normalizedPath(record.certificatePath);
}

void deduplicateKeys(ztermy::ssh::SshKeychainMigrationPlan &plan)
{
    std::vector<ztermy::ssh::SshKeyRecord> unique;
    std::unordered_map<std::string, std::size_t> indexes;
    std::unordered_map<std::string, std::string> replacements;
    unique.reserve(plan.catalog.keys.size());
    for (const ztermy::ssh::SshKeyRecord &record : plan.catalog.keys)
    {
        const std::string identity = keyMaterialIdentity(record);
        const auto existing = indexes.find(identity);
        if (existing == indexes.end())
        {
            indexes.emplace(identity, unique.size());
            unique.push_back(record);
            continue;
        }
        ztermy::ssh::SshKeyRecord &kept = unique[existing->second];
        if (kept.source == ztermy::ssh::SshKeySource::Reference
            && record.source != ztermy::ssh::SshKeySource::Reference)
        {
            if (kept.id != record.id)
            {
                replacements.emplace(kept.id, record.id);
            }
            kept = record;
        }
        else if (record.id != kept.id)
        {
            replacements.emplace(record.id, kept.id);
        }
        plan.changed = true;
    }
    if (replacements.empty())
    {
        return;
    }
    plan.catalog.keys = std::move(unique);
    for (ztermy::ssh::SshIdentity &identity : plan.catalog.identities)
    {
        if (!identity.keyId)
        {
            continue;
        }
        auto replacement = replacements.find(*identity.keyId);
        while (replacement != replacements.end())
        {
            identity.keyId = replacement->second;
            replacement = replacements.find(*identity.keyId);
        }
    }
}

} // namespace

namespace ztermy::ssh
{

std::expected<SshKeychainMigrationPlan, SshKeychainMigrationError>
planLegacyKeychainMigration(const std::vector<SshProfile> &profiles, const SshKeychainCatalog &catalog)
{
    SshKeychainMigrationPlan result{.profiles = profiles, .catalog = catalog};
    deduplicateKeys(result);
    for (SshProfile &profile : result.profiles)
    {
        if (profile.identityReference)
        {
            if (!profile.privateKeyPath.empty() || profile.privateKeyPassphraseRequired || profile.credentialReference)
            {
                profile.privateKeyPath.clear();
                profile.privateKeyPassphraseRequired = false;
                profile.credentialReference.reset();
                result.changed = true;
            }
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
            const auto matchingMaterial =
                std::ranges::find_if(result.catalog.keys, [&expectedKey](const SshKeyRecord &record) {
                    return keyMaterialIdentity(record) == keyMaterialIdentity(expectedKey);
                });
            if (matchingMaterial != result.catalog.keys.end())
            {
                keyId = matchingMaterial->id;
            }
            else
            {
                const auto existingId = std::ranges::find(result.catalog.keys, recordId, &SshKeyRecord::id);
                if (existingId == result.catalog.keys.end() && result.catalog.legacyProfilesMigrated)
                {
                    continue;
                }
                if (existingId == result.catalog.keys.end())
                {
                    result.catalog.keys.push_back(expectedKey);
                    keyId = recordId;
                }
                else if (*existingId != expectedKey)
                {
                    return std::unexpected(SshKeychainMigrationError::ConflictingKey);
                }
                else
                {
                    keyId = existingId->id;
                }
            }
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
        profile.privateKeyPath.clear();
        profile.privateKeyPassphraseRequired = false;
        profile.credentialReference.reset();
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
