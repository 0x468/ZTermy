#pragma once

#include "domain/ssh/SshProfile.h"
#include "infrastructure/ssh/SshKeychainStore.h"

#include <cstdint>
#include <expected>
#include <vector>

namespace ztermy::ssh
{

enum class SshKeychainMigrationError : std::uint8_t
{
    ConflictingIdentity,
    ConflictingKey,
    InvalidResult,
};

struct SshKeychainMigrationPlan final
{
    std::vector<SshProfile> profiles;
    SshKeychainCatalog catalog;
    bool changed = false;
};

[[nodiscard]] std::expected<SshKeychainMigrationPlan, SshKeychainMigrationError>
planLegacyKeychainMigration(const std::vector<SshProfile> &profiles, const SshKeychainCatalog &catalog);

} // namespace ztermy::ssh
