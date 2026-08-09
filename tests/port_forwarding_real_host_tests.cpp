#include "application/forwarding/PortForwardingJob.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

namespace
{

struct RealHostConfiguration final
{
    QString host;
    QString username;
    QString privateKeyPath;
    QString expectedFingerprint;
    std::uint16_t port = 22;
};

[[nodiscard]] std::optional<RealHostConfiguration> configuration()
{
    const QString host = qEnvironmentVariable("ZTERMY_TEST_SSH_HOST");
    const QString username = qEnvironmentVariable("ZTERMY_TEST_SSH_USERNAME");
    const QString privateKeyPath = qEnvironmentVariable("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QString expectedFingerprint = qEnvironmentVariable("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (host.isEmpty() || username.isEmpty() || privateKeyPath.isEmpty() || expectedFingerprint.isEmpty())
    {
        return std::nullopt;
    }
    bool validPort = false;
    const int configuredPort = qEnvironmentVariableIntValue("ZTERMY_TEST_SSH_PORT", &validPort);
    return RealHostConfiguration{.host = host,
                                 .username = username,
                                 .privateKeyPath = privateKeyPath,
                                 .expectedFingerprint = expectedFingerprint,
                                 .port = validPort && configuredPort > 0 && configuredPort <= 65'535
                                             ? static_cast<std::uint16_t>(configuredPort)
                                             : std::uint16_t{22}};
}

[[nodiscard]] std::uint16_t availableLoopbackPort()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
    {
        return 0;
    }
    return server.serverPort();
}

[[nodiscard]] ztermy::ssh::SshConnectionRequest requestFor(const RealHostConfiguration &config,
                                                           const QString &knownHostsPath)
{
    return {
        .host = config.host,
        .port = config.port,
        .username = config.username,
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = config.privateKeyPath,
        .knownHostsPath = knownHostsPath,
    };
}

[[nodiscard]] ztermy::ssh::SshConnectionCallbacks acceptingExpectedHost(const QString &expectedFingerprint)
{
    return {
        .confirmUnknownHostKey =
            [expectedFingerprint](const QString &, const QString &, const QString &fingerprint) {
                return fingerprint == expectedFingerprint ? ztermy::ssh::UnknownHostKeyDecision::AcceptOnce
                                                          : ztermy::ssh::UnknownHostKeyDecision::Reject;
            },
    };
}

[[nodiscard]] bool waitForRunning(ztermy::forwarding::PortForwardingJob &job)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 15'000)
    {
        const auto state = job.snapshot().state;
        if (state == ztermy::forwarding::PortForwardingJobState::Running)
        {
            return true;
        }
        if (state == ztermy::forwarding::PortForwardingJobState::Failed)
        {
            return false;
        }
        QTest::qWait(10);
    }
    return false;
}

[[nodiscard]] bool writeAll(ztermy::ssh::WindowsTcpSocket &socket, const std::span<const char> bytes)
{
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (offset < bytes.size())
    {
        auto written = socket.write(bytes.subspan(offset));
        if (written)
        {
            offset += *written;
            continue;
        }
        if (written.error().kind != ztermy::ssh::SshByteTransportErrorKind::WouldBlock)
        {
            return false;
        }
        if (!socket.waitUntilReady(ztermy::ssh::SocketIoInterest::Write, deadline))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<char> readUntil(ztermy::ssh::WindowsTcpSocket &socket, const std::string_view marker,
                                          const std::size_t minimumBytes)
{
    std::vector<char> result;
    std::array<char, 4'096> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto read = socket.read(buffer);
        if (read && *read > 0)
        {
            result.insert(result.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*read));
            if (result.size() >= minimumBytes
                && std::string_view(result.data(), result.size()).find(marker) != std::string_view::npos)
            {
                break;
            }
            continue;
        }
        if (!read && read.error().kind != ztermy::ssh::SshByteTransportErrorKind::WouldBlock)
        {
            break;
        }
        (void)socket.waitUntilReady(ztermy::ssh::SocketIoInterest::Read, deadline);
    }
    return result;
}

} // namespace

class PortForwardingRealHostTests final : public QObject
{
    Q_OBJECT

private slots:
    void forwardsConcurrentLocalConnections();
    void negotiatesPipelinedDynamicSocksConnection();
};

void PortForwardingRealHostTests::forwardsConcurrentLocalConnections()
{
    const auto config = configuration();
    if (!config)
    {
        QSKIP("Set the SSH real-host variables to run the forwarding gate");
    }
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const std::uint16_t bindPort = availableLoopbackPort();
    QVERIFY(bindPort != 0);

    ztermy::forwarding::PortForwardingJob job;
    const ztermy::forwarding::PortForwardingRule rule{
        .id = "real-local",
        .label = "Real local forwarding",
        .profileId = "real-host",
        .type = ztermy::forwarding::PortForwardingType::Local,
        .bind = {.host = "127.0.0.1", .port = bindPort},
        .destination = {.host = "127.0.0.1", .port = config->port},
    };
    auto started = job.start(rule, requestFor(*config, temporary.filePath(QStringLiteral("known_hosts"))),
                             acceptingExpectedHost(config->expectedFingerprint));
    QVERIFY(started);
    QVERIFY(waitForRunning(job));

    auto first = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", bindPort, 3s);
    auto second = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", bindPort, 3s);
    QVERIFY(first);
    QVERIFY(second);
    const auto firstBanner = readUntil(*first, "SSH-", 4);
    const auto secondBanner = readUntil(*second, "SSH-", 4);
    QVERIFY(std::string_view(firstBanner.data(), firstBanner.size()).find("SSH-") != std::string_view::npos);
    QVERIFY(std::string_view(secondBanner.data(), secondBanner.size()).find("SSH-") != std::string_view::npos);

    job.stop();
    QCOMPARE(job.snapshot().state, ztermy::forwarding::PortForwardingJobState::Stopped);
}

void PortForwardingRealHostTests::negotiatesPipelinedDynamicSocksConnection()
{
    const auto config = configuration();
    if (!config)
    {
        QSKIP("Set the SSH real-host variables to run the forwarding gate");
    }
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const std::uint16_t bindPort = availableLoopbackPort();
    QVERIFY(bindPort != 0);

    ztermy::forwarding::PortForwardingJob job;
    const ztermy::forwarding::PortForwardingRule rule{
        .id = "real-dynamic",
        .label = "Real dynamic forwarding",
        .profileId = "real-host",
        .type = ztermy::forwarding::PortForwardingType::Dynamic,
        .bind = {.host = "127.0.0.1", .port = bindPort},
    };
    auto started = job.start(rule, requestFor(*config, temporary.filePath(QStringLiteral("known_hosts"))),
                             acceptingExpectedHost(config->expectedFingerprint));
    QVERIFY(started);
    QVERIFY(waitForRunning(job));

    auto client = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", bindPort, 3s);
    QVERIFY(client);
    const std::array request{char{0x05},
                             char{0x01},
                             char{0x00},
                             char{0x05},
                             char{0x01},
                             char{0x00},
                             char{0x01},
                             char{127},
                             char{0x00},
                             char{0x00},
                             char{0x01},
                             static_cast<char>((config->port >> 8U) & 0xFFU),
                             static_cast<char>(config->port & 0xFFU)};
    QVERIFY(writeAll(*client, request));

    const auto response = readUntil(*client, "SSH-", 16);
    QVERIFY(response.size() >= 16);
    QCOMPARE(static_cast<unsigned char>(response[0]), 0x05U);
    QCOMPARE(static_cast<unsigned char>(response[1]), 0x00U);
    QCOMPARE(static_cast<unsigned char>(response[2]), 0x05U);
    QCOMPARE(static_cast<unsigned char>(response[3]), 0x00U);
    QVERIFY(std::string_view(response.data() + 12, response.size() - 12).find("SSH-") != std::string_view::npos);

    job.stop();
    QCOMPARE(job.snapshot().state, ztermy::forwarding::PortForwardingJobState::Stopped);
}

QTEST_GUILESS_MAIN(PortForwardingRealHostTests)

#include "port_forwarding_real_host_tests.moc"
