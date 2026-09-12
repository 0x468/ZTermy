#include "application/ssh/SshKeychainMigration.h"

#include <QTest>

#include <vector>

class SshKeychainMigrationTests final : public QObject
{
    Q_OBJECT

private slots:
    void migratesLegacyProfilesWithoutMovingSecrets();
    void isIdempotentAfterProfilesReferenceIdentities();
    void completesAfterCatalogWasSavedBeforeProfiles();
    void leavesNewProfilesOnProfileFieldsAfterMigrationCompleted();
    void deduplicatesKeysByMaterialPath();
    void rejectsCatalogIdConflicts();
};

void SshKeychainMigrationTests::migratesLegacyProfilesWithoutMovingSecrets()
{
    const std::vector<ztermy::ssh::SshProfile> profiles{
        {.id = "password-profile",
         .name = "Password host",
         .host = "password.example.test",
         .username = "operator",
         .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
         .credentialReference = "password-profile"},
        {.id = "key-profile",
         .name = "Key host",
         .host = "key.example.test",
         .username = "root",
         .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
         .privateKeyPath = "C:/Users/test/.ssh/id_ed25519",
         .privateKeyPassphraseRequired = true,
         .credentialReference = "key-profile"},
        {.id = "agent-profile",
         .name = "Agent host",
         .host = "agent.example.test",
         .username = "git",
         .authentication = ztermy::ssh::SshAuthenticationMethod::Agent},
    };

    const auto migrated = ztermy::ssh::planLegacyKeychainMigration(profiles, {});
    QVERIFY(migrated);
    QVERIFY(migrated->changed);
    QCOMPARE(migrated->profiles.size(), std::size_t{3});
    QCOMPARE(migrated->catalog.keys.size(), std::size_t{1});
    QCOMPARE(migrated->catalog.identities.size(), std::size_t{3});
    QCOMPARE(migrated->profiles[1].identityReference, std::optional<std::string>{"key-profile"});
    QVERIFY(migrated->profiles[1].privateKeyPath.empty());
    QVERIFY(!migrated->profiles[1].privateKeyPassphraseRequired);
    QVERIFY(!migrated->profiles[1].credentialReference);
    QCOMPARE(migrated->catalog.keys.front().privateKeyPath, std::string("C:/Users/test/.ssh/id_ed25519"));
    QCOMPARE(migrated->catalog.keys.front().type, ztermy::ssh::SshKeyType::Unknown);
    QCOMPARE(migrated->catalog.identities[0].credentialReference, std::optional<std::string>{"password-profile"});
    QCOMPARE(migrated->catalog.identities[2].authentication, ztermy::ssh::SshIdentityAuthentication::Agent);
}

void SshKeychainMigrationTests::deduplicatesKeysByMaterialPath()
{
    const ztermy::ssh::SshKeychainCatalog catalog{
        .keys = {{.id = "first", .label = "First", .privateKeyPath = R"(C:\Users\test\.ssh\id_ed25519)"},
                 {.id = "second", .label = "Second", .privateKeyPath = "c:/users/test/.ssh/id_ed25519"}},
        .identities = {{.id = "first-identity",
                        .label = "First identity",
                        .username = "root",
                        .authentication = ztermy::ssh::SshIdentityAuthentication::PrivateKey,
                        .keyId = "first"},
                       {.id = "second-identity",
                        .label = "Second identity",
                        .username = "operator",
                        .authentication = ztermy::ssh::SshIdentityAuthentication::PrivateKey,
                        .keyId = "second"}},
        .legacyProfilesMigrated = true,
    };
    const auto migration = ztermy::ssh::planLegacyKeychainMigration({}, catalog);
    QVERIFY(migration);
    QVERIFY(migration->changed);
    QCOMPARE(migration->catalog.keys.size(), std::size_t{1});
    QCOMPARE(migration->catalog.identities[0].keyId, migration->catalog.identities[1].keyId);
}

void SshKeychainMigrationTests::completesAfterCatalogWasSavedBeforeProfiles()
{
    const std::vector<ztermy::ssh::SshProfile> profiles{
        {.id = "profile",
         .name = "Host",
         .host = "host.example.test",
         .username = "root",
         .authentication = ztermy::ssh::SshAuthenticationMethod::Password}};
    const auto first = ztermy::ssh::planLegacyKeychainMigration(profiles, {});
    QVERIFY(first);

    const auto retried = ztermy::ssh::planLegacyKeychainMigration(profiles, first->catalog);
    QVERIFY(retried);
    QVERIFY(retried->changed);
    QCOMPARE(retried->catalog, first->catalog);
    QCOMPARE(retried->profiles, first->profiles);
}

void SshKeychainMigrationTests::leavesNewProfilesOnProfileFieldsAfterMigrationCompleted()
{
    const std::vector<ztermy::ssh::SshProfile> profiles{
        {.id = "new-profile",
         .name = "New host",
         .host = "new.example.test",
         .username = "root",
         .authentication = ztermy::ssh::SshAuthenticationMethod::Password}};
    const ztermy::ssh::SshKeychainCatalog completed{.legacyProfilesMigrated = true};
    const auto migration = ztermy::ssh::planLegacyKeychainMigration(profiles, completed);
    QVERIFY(migration);
    QVERIFY(!migration->changed);
    QCOMPARE(migration->profiles, profiles);
    QCOMPARE(migration->catalog, completed);
}

void SshKeychainMigrationTests::isIdempotentAfterProfilesReferenceIdentities()
{
    std::vector<ztermy::ssh::SshProfile> profiles{{.id = "profile",
                                                   .name = "Host",
                                                   .host = "host.example.test",
                                                   .username = "root",
                                                   .authentication = ztermy::ssh::SshAuthenticationMethod::Password}};
    const auto first = ztermy::ssh::planLegacyKeychainMigration(profiles, {});
    QVERIFY(first);
    const auto second = ztermy::ssh::planLegacyKeychainMigration(first->profiles, first->catalog);
    QVERIFY(second);
    QVERIFY(!second->changed);
    QCOMPARE(second->profiles, first->profiles);
    QCOMPARE(second->catalog, first->catalog);
}

void SshKeychainMigrationTests::rejectsCatalogIdConflicts()
{
    const std::vector<ztermy::ssh::SshProfile> profiles{
        {.id = "profile",
         .name = "Host",
         .host = "host.example.test",
         .username = "root",
         .authentication = ztermy::ssh::SshAuthenticationMethod::Password}};
    const ztermy::ssh::SshKeychainCatalog catalog{
        .identities = {{.id = "profile",
                        .label = "Different identity",
                        .username = "other",
                        .authentication = ztermy::ssh::SshIdentityAuthentication::Password}},
    };
    const auto migrated = ztermy::ssh::planLegacyKeychainMigration(profiles, catalog);
    QVERIFY(!migrated);
    QCOMPARE(migrated.error(), ztermy::ssh::SshKeychainMigrationError::ConflictingIdentity);
}

QTEST_GUILESS_MAIN(SshKeychainMigrationTests)

#include "ssh_keychain_migration_tests.moc"
