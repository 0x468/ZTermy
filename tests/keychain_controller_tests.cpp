#include "application/security/CredentialVaultCoordinator.h"
#include "application/ssh/KeychainController.h"
#include "infrastructure/security/InMemoryCredentialVault.h"
#include "infrastructure/security/PortableCredentialVault.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace
{

[[nodiscard]] std::unique_ptr<ztermy::security::CredentialVaultCoordinator> coordinator(const QString &portablePath)
{
    return std::make_unique<ztermy::security::CredentialVaultCoordinator>(
        std::make_unique<ztermy::security::InMemoryCredentialVault>(),
        std::make_unique<ztermy::security::PortableCredentialVault>(portablePath),
        std::make_unique<ztermy::security::InMemoryCredentialVault>(), ztermy::security::CredentialStorage::System);
}

} // namespace

class KeychainControllerTests final : public QObject
{
    Q_OBJECT

private slots:
    void storesAndRemovesIdentityCredential();
    void generatedManagedKeyIsRemovedWithItsRecord();
};

void KeychainControllerTests::storesAndRemovesIdentityCredential()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto vaults = coordinator(directory.filePath(QStringLiteral("portable.vault")));
    ztermy::ssh::KeychainController controller(directory.filePath(QStringLiteral("keychain.json")),
                                               directory.filePath(QStringLiteral("keys")), {}, vaults.get());

    QVERIFY(controller.saveIdentity({}, QStringLiteral("Production"), QStringLiteral("root"),
                                    QStringLiteral("password"), {}, true, QStringLiteral("secret"), true));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5'000);
    QCOMPARE(controller.operationError(), QString{});
    QCOMPARE(controller.identities().size(), 1);
    const QString identityId = controller.identities().front().toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!identityId.isEmpty());
    QVERIFY(vaults->active().read(
        {.profileId = identityId.toStdString(), .kind = ztermy::security::CredentialKind::Password}));

    QVERIFY(controller.removeIdentity(identityId));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5'000);
    QCOMPARE(controller.identities().size(), 0);
    const auto removed = vaults->active().read(
        {.profileId = identityId.toStdString(), .kind = ztermy::security::CredentialKind::Password});
    QVERIFY(!removed);
    QCOMPARE(removed.error(), ztermy::security::CredentialVaultError::NotFound);
}

void KeychainControllerTests::generatedManagedKeyIsRemovedWithItsRecord()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto vaults = coordinator(directory.filePath(QStringLiteral("portable.vault")));
    ztermy::ssh::KeychainController controller(directory.filePath(QStringLiteral("keychain.json")),
                                               directory.filePath(QStringLiteral("keys")), {}, vaults.get());
    if (!controller.keyGeneratorAvailable())
    {
        QSKIP("Windows OpenSSH ssh-keygen is not installed");
    }

    QVERIFY(controller.generateKey(QStringLiteral("Generated"), QStringLiteral("ed25519"), 0, {}));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 35'000);
    QCOMPARE(controller.operationError(), QString{});
    QCOMPARE(controller.keys().size(), 1);
    const QVariantMap key = controller.keys().front().toMap();
    const QString id = key.value(QStringLiteral("id")).toString();
    const QString privatePath = key.value(QStringLiteral("privateKeyPath")).toString();
    const QString publicPath = key.value(QStringLiteral("publicKeyPath")).toString();
    QVERIFY(QFileInfo::exists(privatePath));
    QVERIFY(QFileInfo::exists(publicPath));

    QVERIFY(controller.removeKey(id));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5'000);
    QCOMPARE(controller.keys().size(), 0);
    QVERIFY(!QFileInfo::exists(privatePath));
    QVERIFY(!QFileInfo::exists(publicPath));
}

QTEST_GUILESS_MAIN(KeychainControllerTests)

#include "keychain_controller_tests.moc"
