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
        .group = "Development",
        .host = "server.example.test",
        .port = 2222,
        .username = "developer",
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = R"(C:\Users\developer\.ssh\id_ed25519)",
        .privateKeyPassphraseRequired = true,
        .credentialReference = "profile-1",
        .lastConnectedUtcMs = 1'754'000'000'123,
        .keywordHighlightRules = {{.id = "failure",
                                   .pattern = "failed",
                                   .foreground = "#FFFFFF",
                                   .background = "#D13438",
                                   .enabled = true,
                                   .caseSensitive = false}},
        .keywordHighlightEnabled = true,
        .sessionOptions = {.terminalType = "screen-256color",
                           .keepaliveIntervalSeconds = 30,
                           .keepaliveFailureThreshold = 4,
                           .startupCommand = "export TERM_PROGRAM=ztermy\nuname -a",
                           .startupCommandMode = ztermy::ssh::SshStartupCommandMode::LineDelay,
                           .startupLineDelayMilliseconds = 175,
                           .environment = {{.name = "LANG", .value = "en_US.UTF-8"},
                                           {.name = "COLORTERM", .value = "truecolor"}},
                           .reconnectPolicy = ztermy::ssh::SshReconnectPolicy::OnTransportFailure,
                           .reconnectMaximumAttempts = 5,
                           .reconnectInitialBackoffMilliseconds = 1500},
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
    void loadsProfilesWrittenBeforePassphraseMetadata();
    void loadsKeywordSchemaWithDefaultSessionOptions();
    void rejectsMalformedSessionOptions();
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
        .group = "Production",
        .host = "password.example.test",
        .port = 22,
        .username = "operator",
        .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
    };
    ztermy::ssh::SshProfile agentProfile{
        .id = "profile-3",
        .name = "Agent host",
        .group = "Production",
        .host = "agent.example.test",
        .port = 22,
        .username = "operator",
        .authentication = ztermy::ssh::SshAuthenticationMethod::Agent,
    };
    const std::array profiles{privateKeyProfile(), passwordProfile, agentProfile};
    const QString path = directory.filePath(QStringLiteral("profiles.json"));
    const ztermy::ssh::SshProfileStore store(path);

    QVERIFY(store.save(profiles));
    auto loaded = store.load();
    QVERIFY(loaded);
    QVERIFY(*loaded == std::vector<ztermy::ssh::SshProfile>(profiles.begin(), profiles.end()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray persisted = file.readAll();
    QVERIFY(!persisted.contains("\"password\":"));
    QVERIFY(!persisted.contains("\"passphrase\":"));
    QVERIFY(persisted.contains("privateKeyPassphraseRequired"));
    QVERIFY(persisted.contains("\"lastConnectedUtcMs\": 1754000000123"));
    QVERIFY(persisted.contains("\"group\": \"Development\""));
    QVERIFY(persisted.contains("\"credentialReference\": \"profile-1\""));
    QVERIFY(persisted.contains("\"keywordHighlightRules\""));
    QVERIFY(persisted.contains("\"pattern\": \"failed\""));
    QVERIFY(persisted.contains("\"sessionOptions\""));
    QVERIFY(persisted.contains("\"terminalType\": \"screen-256color\""));
    QVERIFY(persisted.contains("\"reconnectPolicy\": \"transport-failure\""));
    QVERIFY(persisted.contains("\"authentication\": \"agent\""));
    QVERIFY(!persisted.contains("privateKeyContent"));
}

void SshProfileStoreTests::loadsProfilesWrittenBeforePassphraseMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));
    const ztermy::ssh::SshProfileStore store(path);

    QVERIFY(writeFile(
        path,
        QByteArrayLiteral(
            R"({"version":1,"profiles":[{"id":"p","name":"n","host":"h","port":22,"username":"u","authentication":"private-key","privateKeyPath":"key"}]})")));
    auto profiles = store.load();
    QVERIFY(profiles);
    QCOMPARE(profiles->size(), std::size_t{1});
    QVERIFY(!profiles->front().privateKeyPassphraseRequired);
    QVERIFY(!profiles->front().lastConnectedUtcMs);
    QVERIFY(profiles->front().group.empty());
    QVERIFY(!profiles->front().credentialReference);
    QVERIFY(profiles->front().keywordHighlightRules.empty());
    QVERIFY(profiles->front().keywordHighlightEnabled);
    QCOMPARE(profiles->front().sessionOptions, ztermy::ssh::SshSessionOptions{});
}

void SshProfileStoreTests::loadsKeywordSchemaWithDefaultSessionOptions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));
    const ztermy::ssh::SshProfileStore store(path);

    QVERIFY(writeFile(
        path,
        QByteArrayLiteral(
            R"({"version":3,"profiles":[{"id":"p","name":"n","group":"g","host":"h","port":22,"username":"u","authentication":"password","privateKeyPath":"","keywordHighlightEnabled":true,"keywordHighlightRules":[]}]})")));
    auto profiles = store.load();
    QVERIFY(profiles);
    QCOMPARE(profiles->size(), std::size_t{1});
    QCOMPARE(profiles->front().sessionOptions, ztermy::ssh::SshSessionOptions{});
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

    auto oversizedGroup = privateKeyProfile();
    oversizedGroup.group.assign(129, 'x');
    const std::array oversizedGroupProfiles{oversizedGroup};
    auto oversizedGroupResult = store.save(oversizedGroupProfiles);
    QVERIFY(!oversizedGroupResult);
    QCOMPARE(oversizedGroupResult.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);

    auto negativeTimestamp = privateKeyProfile();
    negativeTimestamp.lastConnectedUtcMs = -1;
    const std::array negativeTimestampProfiles{negativeTimestamp};
    auto negativeTimestampResult = store.save(negativeTimestampProfiles);
    QVERIFY(!negativeTimestampResult);
    QCOMPARE(negativeTimestampResult.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);

    auto duplicateEnvironment = privateKeyProfile();
    duplicateEnvironment.sessionOptions.environment.push_back({.name = "LANG", .value = "C"});
    const std::array duplicateEnvironmentProfiles{duplicateEnvironment};
    auto duplicateEnvironmentResult = store.save(duplicateEnvironmentProfiles);
    QVERIFY(!duplicateEnvironmentResult);
    QCOMPARE(duplicateEnvironmentResult.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);

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

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({"version":5,"profiles":[]})")));
    auto unsupported = store.load();
    QVERIFY(!unsupported);
    QCOMPARE(unsupported.error(), ztermy::ssh::SshProfileStoreError::UnsupportedVersion);
}

void SshProfileStoreTests::rejectsMalformedSessionOptions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profiles.json"));
    const ztermy::ssh::SshProfileStore store(path);

    QVERIFY(writeFile(
        path,
        QByteArrayLiteral(
            R"({"version":4,"profiles":[{"id":"p","name":"n","group":"","host":"h","port":22,"username":"u","authentication":"password","privateKeyPath":"","keywordHighlightEnabled":true,"keywordHighlightRules":[],"sessionOptions":{"terminalType":"xterm-256color","keepaliveIntervalSeconds":30,"keepaliveFailureThreshold":3,"startupCommand":"","startupCommandMode":"unknown","startupLineDelayMilliseconds":100,"environment":[],"reconnectPolicy":"never","reconnectMaximumAttempts":3,"reconnectInitialBackoffMilliseconds":1000}}]})")));
    auto malformedMode = store.load();
    QVERIFY(!malformedMode);
    QCOMPARE(malformedMode.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);
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
            R"({"version":1,"profiles":[{"id":"p","name":"n","host":"h","port":22,"username":"u","authentication":"keyboard-interactive","privateKeyPath":""}]})")));
    auto unknownAuthentication = store.load();
    QVERIFY(!unknownAuthentication);
    QCOMPARE(unknownAuthentication.error(), ztermy::ssh::SshProfileStoreError::InvalidFormat);
}

QTEST_GUILESS_MAIN(SshProfileStoreTests)

#include "ssh_profile_store_tests.moc"
