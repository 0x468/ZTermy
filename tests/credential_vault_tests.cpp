#include "application/security/CredentialVaultCoordinator.h"
#include "core/security/CredentialVault.h"
#include "infrastructure/security/InMemoryCredentialVault.h"
#include "infrastructure/security/PortableCredentialVault.h"
#include "platform/windows/WindowsCredentialVault.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string_view>

namespace
{

[[nodiscard]] ztermy::security::SensitiveByteArray sensitive(const char *text)
{
    return ztermy::security::SensitiveByteArray(QByteArray(text));
}

[[nodiscard]] QByteArray bytes(const ztermy::security::SensitiveByteArray &secret)
{
    const std::string_view view = secret.view();
    return {view.data(), static_cast<qsizetype>(view.size())};
}

[[nodiscard]] ztermy::security::CredentialKey passwordKey(const std::string &profileId = "profile-1")
{
    return {.profileId = profileId, .kind = ztermy::security::CredentialKind::Password};
}

class SingleStoreFailureVault final : public ztermy::security::CredentialVault
{
public:
    void failOnceAfter(const std::size_t successfulStores) noexcept
    {
        m_successfulStoresBeforeFailure = successfulStores;
        m_storeAttempts = 0;
        m_failureArmed = true;
    }

    void failRemoveOnceAfter(const std::size_t successfulRemovals) noexcept
    {
        m_successfulRemovalsBeforeFailure = successfulRemovals;
        m_removeAttempts = 0;
        m_removeFailureArmed = true;
    }

    [[nodiscard]] std::string_view backendId() const noexcept override { return "single-store-failure"; }
    [[nodiscard]] bool persistent() const noexcept override { return false; }

    [[nodiscard]] std::expected<void, ztermy::security::CredentialVaultError>
    store(const ztermy::security::CredentialKey &key, ztermy::security::SensitiveByteArray secret) override
    {
        if (m_failureArmed && m_storeAttempts++ >= m_successfulStoresBeforeFailure)
        {
            m_failureArmed = false;
            return std::unexpected(ztermy::security::CredentialVaultError::IoError);
        }
        return m_delegate.store(key, std::move(secret));
    }

    [[nodiscard]] std::expected<ztermy::security::SensitiveByteArray, ztermy::security::CredentialVaultError>
    read(const ztermy::security::CredentialKey &key) const override
    {
        return m_delegate.read(key);
    }

    [[nodiscard]] std::expected<void, ztermy::security::CredentialVaultError>
    remove(const ztermy::security::CredentialKey &key) override
    {
        if (m_removeFailureArmed && m_removeAttempts++ >= m_successfulRemovalsBeforeFailure)
        {
            m_removeFailureArmed = false;
            return std::unexpected(ztermy::security::CredentialVaultError::IoError);
        }
        return m_delegate.remove(key);
    }

    [[nodiscard]] std::expected<std::vector<ztermy::security::CredentialKey>, ztermy::security::CredentialVaultError>
    listKeys() const override
    {
        return m_delegate.listKeys();
    }

private:
    ztermy::security::InMemoryCredentialVault m_delegate;
    std::size_t m_successfulStoresBeforeFailure = 0;
    std::size_t m_storeAttempts = 0;
    bool m_failureArmed = false;
    std::size_t m_successfulRemovalsBeforeFailure = 0;
    std::size_t m_removeAttempts = 0;
    bool m_removeFailureArmed = false;
};

} // namespace

class CredentialVaultTests final : public QObject
{
    Q_OBJECT

private slots:
    void memoryVaultStoresReplacesAndRemovesSecrets();
    void portableVaultRequiresStrongMasterPassword();
    void portableVaultPersistsAndAuthenticates();
    void portableVaultUsesFreshNonceForEveryRewrite();
    void portableVaultChangesMasterPassword();
    void portableVaultRejectsTampering();
    void windowsVaultRoundTripsGenericCredential();
    void coordinatorMigratesOnlyAfterPortableVaultUnlocks();
    void coordinatorCanLeaveAnUninitializedPortableVault();
    void coordinatorRollsBackPartiallyWrittenTarget();
    void coordinatorRollsBackPartiallyClearedStore();
};

void CredentialVaultTests::memoryVaultStoresReplacesAndRemovesSecrets()
{
    ztermy::security::InMemoryCredentialVault vault;
    const auto key = passwordKey();

    QVERIFY(vault.store(key, sensitive("first")));
    auto first = vault.read(key);
    QVERIFY(first);
    QCOMPARE(bytes(*first), QByteArrayLiteral("first"));

    QVERIFY(vault.store(key, sensitive("second")));
    auto second = vault.read(key);
    QVERIFY(second);
    QCOMPARE(bytes(*second), QByteArrayLiteral("second"));
    QCOMPARE(vault.listKeys()->size(), std::size_t{1});

    QVERIFY(vault.remove(key));
    auto missing = vault.read(key);
    QVERIFY(!missing);
    QCOMPARE(missing.error(), ztermy::security::CredentialVaultError::NotFound);

    const auto empty = vault.store(key, ztermy::security::SensitiveByteArray(QByteArray{}));
    QVERIFY(!empty);
    QCOMPARE(empty.error(), ztermy::security::CredentialVaultError::EmptySecret);
    const auto oversized =
        vault.store(key, ztermy::security::SensitiveByteArray(QByteArray(
                             static_cast<qsizetype>(ztermy::security::MaximumCredentialSecretSize + 1U), 'x')));
    QVERIFY(!oversized);
    QCOMPARE(oversized.error(), ztermy::security::CredentialVaultError::SecretTooLarge);
}

void CredentialVaultTests::portableVaultRequiresStrongMasterPassword()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::security::PortableCredentialVault vault(directory.filePath(QStringLiteral("credentials.zvlt")));

    const auto weak = vault.initialize(sensitive("short"));
    QVERIFY(!weak);
    QCOMPARE(weak.error(), ztermy::security::CredentialVaultError::WeakMasterPassword);
    QVERIFY(!vault.initialized());
}

void CredentialVaultTests::portableVaultPersistsAndAuthenticates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("credentials.zvlt"));
    const auto password = passwordKey();
    const ztermy::security::CredentialKey passphrase{.profileId = "profile-2",
                                                     .kind = ztermy::security::CredentialKind::PrivateKeyPassphrase};

    {
        ztermy::security::PortableCredentialVault vault(path);
        QVERIFY(vault.initialize(sensitive("correct horse battery staple")));
        QVERIFY(!vault.locked());
        QVERIFY(vault.store(password, sensitive("password-secret")));
        QVERIFY(vault.store(passphrase, sensitive("key-secret")));
    }

    QFile encryptedFile(path);
    QVERIFY(encryptedFile.open(QIODevice::ReadOnly));
    const QByteArray encryptedBytes = encryptedFile.readAll();
    QVERIFY(!encryptedBytes.contains("password-secret"));
    QVERIFY(!encryptedBytes.contains("key-secret"));
    QVERIFY(!encryptedBytes.contains("correct horse battery staple"));
    encryptedFile.close();

    ztermy::security::PortableCredentialVault reopened(path);
    QVERIFY(reopened.initialized());
    QVERIFY(reopened.locked());
    const auto lockedRead = reopened.read(password);
    QVERIFY(!lockedRead);
    QCOMPARE(lockedRead.error(), ztermy::security::CredentialVaultError::Locked);

    const auto wrong = reopened.unlock(sensitive("definitely wrong password"));
    QVERIFY(!wrong);
    QCOMPARE(wrong.error(), ztermy::security::CredentialVaultError::AuthenticationFailed);
    QVERIFY(reopened.unlock(sensitive("correct horse battery staple")));

    auto loaded = reopened.read(password);
    QVERIFY(loaded);
    QCOMPARE(bytes(*loaded), QByteArrayLiteral("password-secret"));
    QCOMPARE(reopened.listKeys()->size(), std::size_t{2});
    QVERIFY(reopened.remove(passphrase));
    QCOMPARE(reopened.listKeys()->size(), std::size_t{1});
}

void CredentialVaultTests::portableVaultUsesFreshNonceForEveryRewrite()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("credentials.zvlt"));
    const auto key = passwordKey();
    ztermy::security::PortableCredentialVault vault(path);
    QVERIFY(vault.initialize(sensitive("nonce test master password")));
    QVERIFY(vault.store(key, sensitive("unchanged-secret")));

    QFile firstFile(path);
    QVERIFY(firstFile.open(QIODevice::ReadOnly));
    const QByteArray firstEnvelope = firstFile.readAll();
    firstFile.close();

    QVERIFY(vault.store(key, sensitive("unchanged-secret")));
    QFile secondFile(path);
    QVERIFY(secondFile.open(QIODevice::ReadOnly));
    const QByteArray secondEnvelope = secondFile.readAll();
    QVERIFY(firstEnvelope != secondEnvelope);
}

void CredentialVaultTests::portableVaultChangesMasterPassword()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("credentials.zvlt"));
    const auto key = passwordKey();

    ztermy::security::PortableCredentialVault vault(path);
    QVERIFY(vault.initialize(sensitive("original master password")));
    QVERIFY(vault.store(key, sensitive("saved-password")));
    QVERIFY(vault.changeMasterPassword(sensitive("replacement master password")));
    vault.lock();

    const auto oldPassword = vault.unlock(sensitive("original master password"));
    QVERIFY(!oldPassword);
    QCOMPARE(oldPassword.error(), ztermy::security::CredentialVaultError::AuthenticationFailed);
    QVERIFY(vault.unlock(sensitive("replacement master password")));
    auto loaded = vault.read(key);
    QVERIFY(loaded);
    QCOMPARE(bytes(*loaded), QByteArrayLiteral("saved-password"));
}

void CredentialVaultTests::portableVaultRejectsTampering()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("credentials.zvlt"));
    ztermy::security::PortableCredentialVault vault(path);
    QVERIFY(vault.initialize(sensitive("tamper test master password")));
    QVERIFY(vault.store(passwordKey(), sensitive("saved-password")));
    vault.lock();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.seek(file.size() - 1));
    const QByteArray last = file.read(1);
    QVERIFY(last.size() == 1);
    QVERIFY(file.seek(file.size() - 1));
    const char changed = static_cast<char>(last[0] ^ 0x5a);
    QVERIFY(file.write(&changed, 1) == 1);
    file.close();

    const auto unlocked = vault.unlock(sensitive("tamper test master password"));
    QVERIFY(!unlocked);
    QCOMPARE(unlocked.error(), ztermy::security::CredentialVaultError::AuthenticationFailed);
}

void CredentialVaultTests::windowsVaultRoundTripsGenericCredential()
{
    ztermy::security::WindowsCredentialVault vault;
    const std::string profileId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto key = passwordKey(profileId);

    const auto stored = vault.store(key, sensitive("windows-credential-test"));
    if (!stored && stored.error() == ztermy::security::CredentialVaultError::Unavailable)
    {
        QSKIP("Windows Credential Manager is unavailable in this logon session");
    }
    QVERIFY(stored);

    auto loaded = vault.read(key);
    QVERIFY(loaded);
    QCOMPARE(bytes(*loaded), QByteArrayLiteral("windows-credential-test"));
    auto keys = vault.listKeys();
    QVERIFY(keys);
    QVERIFY(std::ranges::find(*keys, key) != keys->end());
    QVERIFY(vault.remove(key));
    const auto missing = vault.read(key);
    QVERIFY(!missing);
    QCOMPARE(missing.error(), ztermy::security::CredentialVaultError::NotFound);
}

void CredentialVaultTests::coordinatorMigratesOnlyAfterPortableVaultUnlocks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto system = std::make_unique<ztermy::security::InMemoryCredentialVault>();
    auto portable = std::make_unique<ztermy::security::PortableCredentialVault>(
        directory.filePath(QStringLiteral("credentials.zvlt")));
    auto session = std::make_unique<ztermy::security::InMemoryCredentialVault>();
    ztermy::security::CredentialVaultCoordinator coordinator(std::move(system), std::move(portable), std::move(session),
                                                             ztermy::security::CredentialStorage::System);
    const auto key = passwordKey();
    QVERIFY(coordinator.active().store(key, sensitive("migrate-me")));

    const auto unavailable = coordinator.migrate(ztermy::security::CredentialStorage::Portable, true);
    QVERIFY(!unavailable);
    QCOMPARE(unavailable.error(), ztermy::security::CredentialVaultError::Locked);
    QCOMPARE(coordinator.storage(), ztermy::security::CredentialStorage::System);

    QVERIFY(coordinator.initializePortable(sensitive("portable master password")));
    const auto migrated = coordinator.migrate(ztermy::security::CredentialStorage::Portable, true);
    QVERIFY(migrated);
    QCOMPARE(migrated->migrated, std::size_t{1});
    QVERIFY(migrated->sourceCleanupComplete);
    QCOMPARE(coordinator.storage(), ztermy::security::CredentialStorage::Portable);
    auto loaded = coordinator.active().read(key);
    QVERIFY(loaded);
    QCOMPARE(bytes(*loaded), QByteArrayLiteral("migrate-me"));
}

void CredentialVaultTests::coordinatorCanLeaveAnUninitializedPortableVault()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto system = std::make_unique<ztermy::security::InMemoryCredentialVault>();
    auto portable = std::make_unique<ztermy::security::PortableCredentialVault>(
        directory.filePath(QStringLiteral("credentials.zvlt")));
    auto session = std::make_unique<ztermy::security::InMemoryCredentialVault>();
    ztermy::security::CredentialVaultCoordinator coordinator(std::move(system), std::move(portable), std::move(session),
                                                             ztermy::security::CredentialStorage::Portable);

    QVERIFY(!coordinator.portableInitialized());
    const auto cleared = coordinator.removeAll(ztermy::security::CredentialStorage::Portable);
    QVERIFY(cleared);
    QCOMPARE(*cleared, std::size_t{0});
    const auto migrated = coordinator.migrate(ztermy::security::CredentialStorage::System, true);
    QVERIFY(migrated);
    QCOMPARE(migrated->migrated, std::size_t{0});
    QVERIFY(migrated->sourceCleanupComplete);
    QCOMPARE(coordinator.storage(), ztermy::security::CredentialStorage::System);
}

void CredentialVaultTests::coordinatorRollsBackPartiallyWrittenTarget()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto system = std::make_unique<ztermy::security::InMemoryCredentialVault>();
    auto portable = std::make_unique<ztermy::security::PortableCredentialVault>(
        directory.filePath(QStringLiteral("credentials.zvlt")));
    auto session = std::make_unique<SingleStoreFailureVault>();
    SingleStoreFailureVault *target = session.get();
    const auto first = passwordKey("profile-a");
    const auto second = passwordKey("profile-b");

    QVERIFY(system->store(first, sensitive("source-first")));
    QVERIFY(system->store(second, sensitive("source-second")));
    QVERIFY(target->store(first, sensitive("target-original")));
    target->failOnceAfter(1);

    ztermy::security::CredentialVaultCoordinator coordinator(std::move(system), std::move(portable), std::move(session),
                                                             ztermy::security::CredentialStorage::System);
    const auto migrated = coordinator.migrate(ztermy::security::CredentialStorage::Session, false);
    QVERIFY(!migrated);
    QCOMPARE(migrated.error(), ztermy::security::CredentialVaultError::IoError);
    QCOMPARE(coordinator.storage(), ztermy::security::CredentialStorage::System);

    auto sourceFirst = coordinator.active().read(first);
    auto sourceSecond = coordinator.active().read(second);
    QVERIFY(sourceFirst);
    QVERIFY(sourceSecond);
    QCOMPARE(bytes(*sourceFirst), QByteArrayLiteral("source-first"));
    QCOMPARE(bytes(*sourceSecond), QByteArrayLiteral("source-second"));

    auto restored = target->read(first);
    QVERIFY(restored);
    QCOMPARE(bytes(*restored), QByteArrayLiteral("target-original"));
    const auto absent = target->read(second);
    QVERIFY(!absent);
    QCOMPARE(absent.error(), ztermy::security::CredentialVaultError::NotFound);
}

void CredentialVaultTests::coordinatorRollsBackPartiallyClearedStore()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto system = std::make_unique<SingleStoreFailureVault>();
    SingleStoreFailureVault *selected = system.get();
    auto portable = std::make_unique<ztermy::security::PortableCredentialVault>(
        directory.filePath(QStringLiteral("credentials.zvlt")));
    auto session = std::make_unique<ztermy::security::InMemoryCredentialVault>();
    const auto first = passwordKey("profile-a");
    const auto second = passwordKey("profile-b");

    QVERIFY(selected->store(first, sensitive("first-secret")));
    QVERIFY(selected->store(second, sensitive("second-secret")));
    selected->failRemoveOnceAfter(1);

    ztermy::security::CredentialVaultCoordinator coordinator(std::move(system), std::move(portable), std::move(session),
                                                             ztermy::security::CredentialStorage::System);
    const auto cleared = coordinator.removeAll(ztermy::security::CredentialStorage::System);
    QVERIFY(!cleared);
    QCOMPARE(cleared.error(), ztermy::security::CredentialVaultError::IoError);

    auto restoredFirst = coordinator.active().read(first);
    auto restoredSecond = coordinator.active().read(second);
    QVERIFY(restoredFirst);
    QVERIFY(restoredSecond);
    QCOMPARE(bytes(*restoredFirst), QByteArrayLiteral("first-secret"));
    QCOMPARE(bytes(*restoredSecond), QByteArrayLiteral("second-secret"));
}

QTEST_GUILESS_MAIN(CredentialVaultTests)

#include "credential_vault_tests.moc"
