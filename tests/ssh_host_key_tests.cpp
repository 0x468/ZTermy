#include "domain/ssh/SshHostKey.h"

#include <QTest>

#include <array>
#include <cstdint>
#include <vector>

namespace
{

using ztermy::ssh::HostKeyAlgorithm;
using ztermy::ssh::HostKeyTrust;
using ztermy::ssh::KnownHostEntry;
using ztermy::ssh::ObservedHostKey;
using ztermy::ssh::SshEndpoint;

[[nodiscard]] ObservedHostKey observedKey(const std::uint8_t marker = 1)
{
    ObservedHostKey result{
        .algorithm = HostKeyAlgorithm::Ed25519,
        .encodedKey = {marker, 2, 3, 4},
    };
    for (std::size_t index = 0; index < result.sha256.size(); ++index)
    {
        result.sha256[index] = static_cast<std::uint8_t>(index);
    }
    return result;
}

[[nodiscard]] KnownHostEntry knownEntry(const SshEndpoint &endpoint, const ObservedHostKey &observed)
{
    return {
        .endpoint = endpoint,
        .algorithm = observed.algorithm,
        .encodedKey = observed.encodedKey,
    };
}

} // namespace

class SshHostKeyTests final : public QObject
{
    Q_OBJECT

private slots:
    void matchesExactHostPortAndKey();
    void reportsUnknownHostOrPort();
    void blocksChangedKeyOrAlgorithm();
    void rejectsMalformedInput();
    void formatsOpenSshStyleSha256Fingerprint();
    void namesSupportedAlgorithms();
};

void SshHostKeyTests::matchesExactHostPortAndKey()
{
    const SshEndpoint endpoint{.host = "example.test", .port = 22};
    const ObservedHostKey observed = observedKey();
    const std::array knownHosts{knownEntry(endpoint, observed)};

    QCOMPARE(ztermy::ssh::evaluateHostKeyTrust(endpoint, observed, knownHosts), HostKeyTrust::Trusted);
}

void SshHostKeyTests::reportsUnknownHostOrPort()
{
    const SshEndpoint endpoint{.host = "example.test", .port = 22};
    const ObservedHostKey observed = observedKey();
    const std::array knownHosts{knownEntry(SshEndpoint{.host = "other.test", .port = 22}, observed)};

    QCOMPARE(ztermy::ssh::evaluateHostKeyTrust(endpoint, observed, knownHosts), HostKeyTrust::Unknown);
    QCOMPARE(ztermy::ssh::evaluateHostKeyTrust(SshEndpoint{.host = "other.test", .port = 2222}, observed, knownHosts),
             HostKeyTrust::Unknown);
}

void SshHostKeyTests::blocksChangedKeyOrAlgorithm()
{
    const SshEndpoint endpoint{.host = "example.test", .port = 22};
    const ObservedHostKey observed = observedKey();

    KnownHostEntry changedKey = knownEntry(endpoint, observed);
    changedKey.encodedKey.front() ^= 0xff;
    const std::array changedKeyEntries{changedKey};
    QCOMPARE(ztermy::ssh::evaluateHostKeyTrust(endpoint, observed, changedKeyEntries), HostKeyTrust::Changed);

    KnownHostEntry changedAlgorithm = knownEntry(endpoint, observed);
    changedAlgorithm.algorithm = HostKeyAlgorithm::Rsa;
    const std::array changedAlgorithmEntries{changedAlgorithm};
    QCOMPARE(ztermy::ssh::evaluateHostKeyTrust(endpoint, observed, changedAlgorithmEntries), HostKeyTrust::Changed);
}

void SshHostKeyTests::rejectsMalformedInput()
{
    const SshEndpoint endpoint{.host = "example.test", .port = 22};
    ObservedHostKey invalidObserved = observedKey();
    invalidObserved.encodedKey.clear();
    QCOMPARE(ztermy::ssh::evaluateHostKeyTrust(endpoint, invalidObserved, {}), HostKeyTrust::Invalid);

    const KnownHostEntry malformedEntry{.endpoint = endpoint};
    const std::array knownHosts{malformedEntry};
    QCOMPARE(ztermy::ssh::evaluateHostKeyTrust(endpoint, observedKey(), knownHosts), HostKeyTrust::Invalid);
}

void SshHostKeyTests::formatsOpenSshStyleSha256Fingerprint()
{
    const ObservedHostKey observed = observedKey();
    QCOMPARE(QString::fromStdString(ztermy::ssh::sha256Fingerprint(observed)),
             QStringLiteral("SHA256:AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8"));
}

void SshHostKeyTests::namesSupportedAlgorithms()
{
    QCOMPARE(ztermy::ssh::hostKeyAlgorithmName(HostKeyAlgorithm::Rsa), std::string_view("RSA"));
    QCOMPARE(ztermy::ssh::hostKeyAlgorithmName(HostKeyAlgorithm::EcdsaP256), std::string_view("ECDSA P-256"));
    QCOMPARE(ztermy::ssh::hostKeyAlgorithmName(HostKeyAlgorithm::Ed25519), std::string_view("ED25519"));
    QCOMPARE(ztermy::ssh::hostKeyAlgorithmName(HostKeyAlgorithm::Unknown), std::string_view("Unknown"));
}

QTEST_GUILESS_MAIN(SshHostKeyTests)

#include "ssh_host_key_tests.moc"
