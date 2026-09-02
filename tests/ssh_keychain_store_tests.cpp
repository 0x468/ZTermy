#include "domain/ssh/SshKeychain.h"
#include "infrastructure/ssh/SshKeychainStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{

[[nodiscard]] ztermy::ssh::SshKeyRecord keyRecord()
{
    return {
        .id = "key-1",
        .label = "Primary ED25519",
        .type = ztermy::ssh::SshKeyType::Ed25519,
        .kind = ztermy::ssh::SshKeyKind::Key,
        .source = ztermy::ssh::SshKeySource::Reference,
        .privateKeyPath = "C:/Users/test/.ssh/id_ed25519",
        .publicKeyPath = "C:/Users/test/.ssh/id_ed25519.pub",
        .createdUtcMs = 1,
    };
}

[[nodiscard]] ztermy::ssh::SshIdentity identity()
{
    return {
        .id = "identity-1",
        .label = "Production root",
        .username = "root",
        .authentication = ztermy::ssh::SshIdentityAuthentication::PrivateKey,
        .keyId = "key-1",
        .credentialReference = "identity-1",
        .createdUtcMs = 2,
    };
}

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

} // namespace

class SshKeychainStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void missingFileLoadsAsEmpty();
    void savesAndLoadsCatalog();
    void rejectsMissingAndMismatchedKeyReferences();
    void rejectsFutureSchemasWithoutRewriting();
};

void SshKeychainStoreTests::missingFileLoadsAsEmpty()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const ztermy::ssh::SshKeychainStore store(directory.filePath(QStringLiteral("keychain.json")));
    const auto catalog = store.load();
    QVERIFY(catalog);
    QVERIFY(catalog->keys.empty());
    QVERIFY(catalog->identities.empty());
}

void SshKeychainStoreTests::savesAndLoadsCatalog()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const ztermy::ssh::SshKeychainStore store(directory.filePath(QStringLiteral("keychain.json")));
    const ztermy::ssh::SshKeychainCatalog expected{.keys = {keyRecord()}, .identities = {identity()}};
    QVERIFY(store.save(expected));
    const auto actual = store.load();
    QVERIFY(actual);
    QCOMPARE(*actual, expected);
}

void SshKeychainStoreTests::rejectsMissingAndMismatchedKeyReferences()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const ztermy::ssh::SshKeychainStore store(directory.filePath(QStringLiteral("keychain.json")));
    ztermy::ssh::SshKeychainCatalog missing{.identities = {identity()}};
    auto missingResult = store.save(missing);
    QVERIFY(!missingResult);
    QCOMPARE(missingResult.error(), ztermy::ssh::SshKeychainStoreError::InvalidFormat);

    auto certificateIdentity = identity();
    certificateIdentity.authentication = ztermy::ssh::SshIdentityAuthentication::Certificate;
    ztermy::ssh::SshKeychainCatalog mismatched{.keys = {keyRecord()}, .identities = {certificateIdentity}};
    auto mismatchedResult = store.save(mismatched);
    QVERIFY(!mismatchedResult);
    QCOMPARE(mismatchedResult.error(), ztermy::ssh::SshKeychainStoreError::InvalidFormat);
}

void SshKeychainStoreTests::rejectsFutureSchemasWithoutRewriting()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("keychain.json"));
    const QByteArray future = QByteArrayLiteral(R"({"version":99,"keys":[],"identities":[]})");
    QVERIFY(writeFile(path, future));
    const ztermy::ssh::SshKeychainStore store(path);
    const auto loaded = store.load();
    QVERIFY(!loaded);
    QCOMPARE(loaded.error(), ztermy::ssh::SshKeychainStoreError::UnsupportedVersion);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), future);
}

QTEST_GUILESS_MAIN(SshKeychainStoreTests)

#include "ssh_keychain_store_tests.moc"
