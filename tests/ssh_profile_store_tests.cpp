#include "domain/ssh/SshProfile.h"
#include "infrastructure/ssh/SshProfileStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <vector>

namespace
{

[[nodiscard]] ztermy::ssh::SshProfile privateKeyProfile(std::string id = "profile-1")
{
    return {
        .id = std::move(id),
        .name = "Lab server",
        .host = "server.example.test",
        .port = 2222,
        .username = "developer",
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = R"(C:\Users\developer\.ssh\id_ed25519)",
    };
}

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

} // namespace

class SshProfileStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void missingFileLoadsAsEmpty();
    void savesAndLoadsNonSecretProfiles();
    void createsMissingParentDirectory();
    void rejectsInvalidProfilesAndDuplicateIds();
    void rejectsMalformedAndUnsupportedDocuments();
    void rejectsFractionalPortsAndUnknownAuthentication();
};

void SshProfileStoreTests::missingFileLoadsAsEmpty()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const ztermy::ssh::SshProfileStore store(directory.filePath(QStringLiteral("profiles.json")));
    auto profiles = store.load();
    QVERIFY(profiles);
    QVERIFY(profiles->empty());
}

void SshProfileStoreTests::savesAndLoadsNonSecretProfiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    ztermy::ssh::SshProfile passwordProfile{
        .id = "profile-2",
        .name = "Password host",
        .host = "password.example.test",
        .port = 22,
        .username = "operator",
        .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
    };
    const std::array profiles{privateKeyProfile(), passwordProfile};
    const QString path = directory.filePath(QStringLiteral("profiles.json"));
    const ztermy::ssh::SshProfileStore store(path);

    QVERIFY(store.save(profiles));
    auto loaded = store.load();
    QVERIFY(loaded);
    QVERIFY(*loaded == std::vector<ztermy::ssh::SshProfile>(profiles.begin(), profiles.end()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray persisted = file.readAll();
    QVERIFY(!persisted.contains("passphrase"));
    QVERIFY(!persisted.contains("privateKeyContent"));
}

void SshProfileStoreTests::createsMissingParentDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = directory.filePath(QStringLiteral("nested/config/profiles.json"));
    const ztermy::ssh::SshProfileStore store(path);
    const std::array profiles{privateKeyProfile()};

    QVERIFY(store.save(profiles));
    QVERIFY(QFile::exists(path));
}

void SshProfileStoreTests::rejectsInvalidProfilesAndDuplicateIds()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const ztermy::ssh::SshProfileStore store(directory.filePath(QStringLiteral("profiles.json")));

    auto invalid = privateKeyProfile();
    invalid.privateKeyPath.clear();
    const std::array invalidProfiles{invalid};
    auto invalidResult = store.save(invalidProfiles);
    QVERIFY(!invalidResult);
    QCOMPARE(invalidResult.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);

    const auto profile = privateKeyProfile();
    const std::array duplicateProfiles{profile, profile};
    auto duplicateResult = store.save(duplicateProfiles);
    QVERIFY(!duplicateResult);
    QCOMPARE(duplicateResult.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);
}

void SshProfileStoreTests::rejectsMalformedAndUnsupportedDocuments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));
    const ztermy::ssh::SshProfileStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral("{not-json")));
    auto malformed = store.load();
    QVERIFY(!malformed);
    QCOMPARE(malformed.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({"version":2,"profiles":[]})")));
    auto unsupported = store.load();
    QVERIFY(!unsupported);
    QCOMPARE(unsupported.error(), ztermy::ssh::SshProfileStoreError::UnsupportedVersion);
}

void SshProfileStoreTests::rejectsFractionalPortsAndUnknownAuthentication()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));
    const ztermy::ssh::SshProfileStore store(path);

    QVERIFY(writeFile(
        path,
        QByteArrayLiteral(
            R"({"version":1,"profiles":[{"id":"p","name":"n","host":"h","port":22.5,"username":"u","authentication":"private-key","privateKeyPath":"key"}]})")));
    auto fractionalPort = store.load();
    QVERIFY(!fractionalPort);
    QCOMPARE(fractionalPort.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);

    QVERIFY(writeFile(
        path,
        QByteArrayLiteral(
            R"({"version":1,"profiles":[{"id":"p","name":"n","host":"h","port":22,"username":"u","authentication":"agent","privateKeyPath":""}]})")));
    auto unknownAuthentication = store.load();
    QVERIFY(!unknownAuthentication);
    QCOMPARE(unknownAuthentication.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);
}

QTEST_GUILESS_MAIN(SshProfileStoreTests)

#include "ssh_profile_store_tests.moc"
