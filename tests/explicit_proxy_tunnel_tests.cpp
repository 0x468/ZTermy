#include "infrastructure/ssh/ExplicitProxyTunnel.h"

#include <QtTest>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <new>
#include <span>
#include <string>
#include <utility>

using namespace std::chrono_literals;

namespace
{

class ScriptedTransport final : public ztermy::ssh::SshByteTransport
{
public:
    explicit ScriptedTransport(std::string response, const std::size_t maximumChunk = 2)
        : m_response(std::move(response)), m_maximumChunk(maximumChunk)
    {
    }

    [[nodiscard]] bool valid() const noexcept override { return m_valid; }

    [[nodiscard]] std::expected<std::size_t, ztermy::ssh::SshByteTransportError>
    read(const std::span<char> buffer) noexcept override
    {
        if (m_readWouldBlock)
        {
            m_readWouldBlock = false;
            return std::unexpected(ztermy::ssh::SshByteTransportError{
                .kind = ztermy::ssh::SshByteTransportErrorKind::WouldBlock,
            });
        }
        m_readWouldBlock = m_alternateWouldBlock;
        if (m_readOffset >= m_response.size())
        {
            return std::unexpected(ztermy::ssh::SshByteTransportError{
                .kind = ztermy::ssh::SshByteTransportErrorKind::Closed,
            });
        }
        const std::size_t count = (std::min)({buffer.size(), m_maximumChunk, m_response.size() - m_readOffset});
        std::ranges::copy_n(m_response.begin() + static_cast<std::ptrdiff_t>(m_readOffset),
                            static_cast<std::ptrdiff_t>(count), buffer.begin());
        m_readOffset += count;
        return count;
    }

    [[nodiscard]] std::expected<std::size_t, ztermy::ssh::SshByteTransportError>
    write(const std::span<const char> buffer) noexcept override
    {
        if (m_writeWouldBlock)
        {
            m_writeWouldBlock = false;
            return std::unexpected(ztermy::ssh::SshByteTransportError{
                .kind = ztermy::ssh::SshByteTransportErrorKind::WouldBlock,
            });
        }
        m_writeWouldBlock = m_alternateWouldBlock;
        const std::size_t count = (std::min)(buffer.size(), m_maximumChunk);
        try
        {
            m_written.append(buffer.data(), count);
        }
        catch (const std::bad_alloc &)
        {
            return std::unexpected(ztermy::ssh::SshByteTransportError{
                .kind = ztermy::ssh::SshByteTransportErrorKind::SystemError,
            });
        }
        return count;
    }

    [[nodiscard]] std::expected<void, ztermy::ssh::SshByteTransportError>
    waitUntilReady(ztermy::ssh::SocketIoInterest, std::chrono::steady_clock::time_point,
                   const std::stop_token &stopToken, std::uintptr_t) noexcept override
    {
        ++m_waitCount;
        if (stopToken.stop_requested())
        {
            return std::unexpected(ztermy::ssh::SshByteTransportError{
                .kind = ztermy::ssh::SshByteTransportErrorKind::Cancelled,
            });
        }
        return {};
    }

    void alternateWouldBlock() noexcept
    {
        m_alternateWouldBlock = true;
        m_readWouldBlock = true;
        m_writeWouldBlock = true;
    }

    [[nodiscard]] const std::string &written() const noexcept { return m_written; }
    [[nodiscard]] std::size_t waitCount() const noexcept { return m_waitCount; }

private:
    std::string m_response;
    std::string m_written;
    std::size_t m_readOffset = 0;
    std::size_t m_maximumChunk = 2;
    std::size_t m_waitCount = 0;
    bool m_valid = true;
    bool m_alternateWouldBlock = false;
    bool m_readWouldBlock = false;
    bool m_writeWouldBlock = false;
};

[[nodiscard]] std::string bytes(const std::initializer_list<unsigned int> values)
{
    std::string result;
    result.reserve(values.size());
    for (const unsigned int value : values)
    {
        result.push_back(static_cast<char>(value));
    }
    return result;
}

} // namespace

class ExplicitProxyTunnelTests final : public QObject
{
    Q_OBJECT

private slots:
    void opensAnonymousSocks5TunnelWithPartialIo();
    void authenticatesSocks5WithoutLeakingPastTheHandshake();
    void distinguishesSocks5AuthenticationAndConnectionRejection();
    void opensHttpConnectTunnelAndPreservesTheSshBanner();
    void sendsHttpBasicAuthenticationAndMaps407();
    void rejectsUnsafeOrUnboundedConfiguration();
};

void ExplicitProxyTunnelTests::opensAnonymousSocks5TunnelWithPartialIo()
{
    ScriptedTransport transport(bytes({5, 0, 5, 0, 0, 1, 127, 0, 0, 1, 0x1F, 0x90}), 1);
    transport.alternateWouldBlock();

    const auto result = ztermy::ssh::establishExplicitProxyTunnel(transport, ztermy::ssh::SshProxyType::Socks5,
                                                                  "server.internal", 22, {}, {}, 1s);

    QVERIFY(result.has_value());
    QCOMPARE(transport.waitCount() > 0, true);
    const std::string expected = bytes({5, 1, 0, 5, 1, 0, 3, 15}) + "server.internal" + bytes({0, 22});
    QCOMPARE(transport.written(), expected);
}

void ExplicitProxyTunnelTests::authenticatesSocks5WithoutLeakingPastTheHandshake()
{
    ScriptedTransport transport(bytes({5, 2, 1, 0, 5, 0, 0, 3, 3}) + "bnd" + bytes({0, 22}));

    const auto result = ztermy::ssh::establishExplicitProxyTunnel(transport, ztermy::ssh::SshProxyType::Socks5, "host",
                                                                  2222, "user", "pass", 1s);

    QVERIFY(result.has_value());
    const std::string expected =
        bytes({5, 1, 2, 1, 4}) + "user" + bytes({4}) + "pass" + bytes({5, 1, 0, 3, 4}) + "host" + bytes({0x08, 0xAE});
    QCOMPARE(transport.written(), expected);
}

void ExplicitProxyTunnelTests::distinguishesSocks5AuthenticationAndConnectionRejection()
{
    ScriptedTransport authenticationRejected(bytes({5, 2, 1, 1}));
    auto result = ztermy::ssh::establishExplicitProxyTunnel(authenticationRejected, ztermy::ssh::SshProxyType::Socks5,
                                                            "host", 22, "u", "p", 1s);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().kind, ztermy::ssh::ExplicitProxyErrorKind::AuthenticationRejected);

    ScriptedTransport connectionRejected(bytes({5, 0, 5, 5, 0, 1}));
    result = ztermy::ssh::establishExplicitProxyTunnel(connectionRejected, ztermy::ssh::SshProxyType::Socks5, "host",
                                                       22, {}, {}, 1s);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().kind, ztermy::ssh::ExplicitProxyErrorKind::ConnectionRejected);
    QCOMPARE(result.error().protocolCode, 5);
}

void ExplicitProxyTunnelTests::opensHttpConnectTunnelAndPreservesTheSshBanner()
{
    ScriptedTransport transport("HTTP/1.1 200 Connection Established\r\nX-Test: yes\r\n\r\nSSH-2.0-test\r\n", 64);

    const auto result = ztermy::ssh::establishExplicitProxyTunnel(transport, ztermy::ssh::SshProxyType::HttpConnect,
                                                                  "server.internal", 22, {}, {}, 1s);

    QVERIFY(result.has_value());
    QCOMPARE(transport.written(), std::string("CONNECT server.internal:22 HTTP/1.1\r\nHost: server.internal:22\r\n"
                                              "Proxy-Connection: Keep-Alive\r\n\r\n"));
    std::array<char, 4> banner{};
    const auto received = transport.read(banner);
    QVERIFY(received.has_value());
    QCOMPARE(*received, std::size_t{4});
    QCOMPARE(std::string_view(banner.data(), banner.size()), std::string_view("SSH-"));
}

void ExplicitProxyTunnelTests::sendsHttpBasicAuthenticationAndMaps407()
{
    ScriptedTransport accepted("HTTP/1.1 204 No Content\r\n\r\n", 64);
    auto result = ztermy::ssh::establishExplicitProxyTunnel(accepted, ztermy::ssh::SshProxyType::HttpConnect, "host",
                                                            443, "user", "pass", 1s);
    QVERIFY(result.has_value());
    QVERIFY(accepted.written().find("Proxy-Authorization: Basic dXNlcjpwYXNz\r\n") != std::string::npos);

    ScriptedTransport rejected("HTTP/1.1 407 Proxy Authentication Required\r\n\r\n", 64);
    result = ztermy::ssh::establishExplicitProxyTunnel(rejected, ztermy::ssh::SshProxyType::HttpConnect, "host", 22,
                                                       "user", "wrong", 1s);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().kind, ztermy::ssh::ExplicitProxyErrorKind::AuthenticationRejected);
    QCOMPARE(result.error().protocolCode, 407);
}

void ExplicitProxyTunnelTests::rejectsUnsafeOrUnboundedConfiguration()
{
    ScriptedTransport transport("unused");
    auto result = ztermy::ssh::establishExplicitProxyTunnel(transport, ztermy::ssh::SshProxyType::HttpConnect,
                                                            "host\r\nInjected: yes", 22, {}, {}, 1s);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().kind, ztermy::ssh::ExplicitProxyErrorKind::InvalidConfiguration);

    const std::string oversized(256, 'a');
    result = ztermy::ssh::establishExplicitProxyTunnel(transport, ztermy::ssh::SshProxyType::Socks5, oversized, 22, {},
                                                       {}, 1s);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().kind, ztermy::ssh::ExplicitProxyErrorKind::InvalidConfiguration);
}

QTEST_GUILESS_MAIN(ExplicitProxyTunnelTests)

#include "explicit_proxy_tunnel_tests.moc"
