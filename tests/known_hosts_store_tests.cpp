#include "domain/ssh/SshHostKey.h"
#include "infrastructure/ssh/KnownHostsStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <cstdint>
#include <vector>

namespace
{

[[nodiscard]] ztermy::ssh::ObservedHostKey observedKey(const std::uint8_t marker = 1)
{
    return {
        .algorithm = ztermy::ssh::HostKeyAlgorithm::Ed25519,
        .encodedKey = {marker, 2, 3, 4},
    };
}

[[nodiscard]] ztermy::ssh::KnownHostEntry knownEntry(const ztermy::ssh::SshEndpoint &endpoint,
                                                     const ztermy::ssh::ObservedHostKey &observed)
{
    return {
        .endpoint = endpoint,
        .algorithm = observed.algorithm,
        .encodedKey = observed.encodedKey,
    };
}

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

class KnownHostsStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void missingFileLoadsAsEmpty();
    void savesAndLoadsTrustedEntries();
    void createsMissingParentDirectory();
    void rejectsChangedKeysAfterReload();
    void rejectsMalformedAndUnsupportedDocuments();
    void rejectsDuplicateEndpointAlgorithms();
    void rejectsFractionalPortsAndInvalidBase64();
};

void KnownHostsStoreTests::missingFileLoadsAsEmpty()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const ztermy::ssh::KnownHostsStore store(directory.filePath(QStringLiteral("known_hosts.json")));
    auto entries = store.load();
    QVERIFY(entries);
    QVERIFY(entries->empty());
}

void KnownHostsStoreTests::savesAndLoadsTrustedEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const ztermy::ssh::SshEndpoint endpoint{.host = "server.example.test", .port = 22};
    const ztermy::ssh::ObservedHostKey observed = observedKey();
    const std::array entries{knownEntry(endpoint, observed)};
    const ztermy::ssh::KnownHostsStore store(directory.filePath(QStringLiteral("known_hosts.json")));

    QVERIFY(store.save(entries));
    auto loaded = store.load();
    QVERIFY(loaded);
    QVERIFY(*loaded == std::vector<ztermy::ssh::KnownHostEntry>(entries.begin(), entries.end()));
    QCOMPARE(ztermy::ssh::evaluateHostKeyTrust(endpoint, observed, *loaded), ztermy::ssh::HostKeyTrust::Trusted);
}

void KnownHostsStoreTests::createsMissingParentDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = directory.filePath(QStringLiteral("nested/config/known_hosts.json"));
    const ztermy::ssh::KnownHostsStore store(path);
    const std::array entries{
        knownEntry(ztermy::ssh::SshEndpoint{.host = "server.example.test"}, observedKey()),
    };

    QVERIFY(store.save(entries));
    QVERIFY(QFile::exists(path));
}

void KnownHostsStoreTests::rejectsChangedKeysAfterReload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const ztermy::ssh::SshEndpoint endpoint{.host = "server.example.test", .port = 22};
    const std::array entries{knownEntry(endpoint, observedKey(1))};
    const ztermy::ssh::KnownHostsStore store(directory.filePath(QStringLiteral("known_hosts.json")));
    QVERIFY(store.save(entries));

    auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(ztermy::ssh::evaluateHostKeyTrust(endpoint, observedKey(9), *loaded), ztermy::ssh::HostKeyTrust::Changed);
}

void KnownHostsStoreTests::rejectsMalformedAndUnsupportedDocuments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("known_hosts.json"));
    const ztermy::ssh::KnownHostsStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral("{not-json")));
    auto malformed = store.load();
    QVERIFY(!malformed);
    QCOMPARE(malformed.error(), ztermy::ssh::KnownHostsStoreError::InvalidFormat);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({"version":2,"hosts":[]})")));
    auto unsupported = store.load();
    QVERIFY(!unsupported);
    QCOMPARE(unsupported.error(), ztermy::ssh::KnownHostsStoreError::UnsupportedVersion);
}

void KnownHostsStoreTests::rejectsDuplicateEndpointAlgorithms()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const ztermy::ssh::SshEndpoint endpoint{.host = "server.example.test", .port = 22};
    const ztermy::ssh::KnownHostEntry entry = knownEntry(endpoint, observedKey());
    const std::array entries{entry, entry};
    const ztermy::ssh::KnownHostsStore store(directory.filePath(QStringLiteral("known_hosts.json")));

    auto result = store.save(entries);
    QVERIFY(!result);
    QCOMPARE(result.error(), ztermy::ssh::KnownHostsStoreError::InvalidFormat);
}

void KnownHostsStoreTests::rejectsFractionalPortsAndInvalidBase64()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("known_hosts.json"));
    const ztermy::ssh::KnownHostsStore store(path);

    QVERIFY(writeFile(
        path, QByteArrayLiteral(
                  R"({"version":1,"hosts":[{"host":"server","port":22.5,"algorithm":"ssh-ed25519","key":"AQID"}]})")));
    auto fractionalPort = store.load();
    QVERIFY(!fractionalPort);
    QCOMPARE(fractionalPort.error(), ztermy::ssh::KnownHostsStoreError::InvalidFormat);

    QVERIFY(writeFile(
        path, QByteArrayLiteral(
                  R"({"version":1,"hosts":[{"host":"server","port":22,"algorithm":"ssh-ed25519","key":"%%%"}]})")));
    auto invalidBase64 = store.load();
    QVERIFY(!invalidBase64);
    QCOMPARE(invalidBase64.error(), ztermy::ssh::KnownHostsStoreError::InvalidFormat);
}

QTEST_GUILESS_MAIN(KnownHostsStoreTests)

#include "known_hosts_store_tests.moc"
