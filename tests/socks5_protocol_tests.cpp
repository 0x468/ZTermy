#include "domain/forwarding/Socks5Protocol.h"

#include <QtTest>

#include <array>
#include <span>
#include <vector>

namespace
{

using namespace ztermy::forwarding;

[[nodiscard]] std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values)
{
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const std::uint8_t value : values)
    {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

class Socks5ProtocolTests final : public QObject
{
    Q_OBJECT

private slots:
    void parsesFragmentedDomainRequestAndPreservesPipelinedBytes();
    void parsesIpv4AndIpv6Destinations();
    void rejectsAuthenticationCommandAddressAndSizeViolations();
    void emitsBoundedReplies();
};

void Socks5ProtocolTests::parsesFragmentedDomainRequestAndPreservesPipelinedBytes()
{
    Socks5Handshake handshake;
    const auto greetingPrefix = bytes({0x05, 0x02, 0x02});
    auto result = handshake.consume(greetingPrefix);
    QVERIFY(result);
    QCOMPARE(result->status, Socks5HandshakeStatus::NeedMore);

    const auto greetingSuffix = bytes({0x00});
    result = handshake.consume(greetingSuffix);
    QVERIFY(result);
    QCOMPARE(result->status, Socks5HandshakeStatus::SendMethodSelection);
    QCOMPARE(result->response, bytes({0x05, 0x00}));

    const QByteArray domain = QByteArrayLiteral("example.test");
    auto request = bytes({0x05, 0x01, 0x00, 0x03, static_cast<std::uint8_t>(domain.size())});
    for (const char character : domain)
    {
        request.push_back(static_cast<std::byte>(character));
    }
    const auto suffix = bytes({0x01, 0xBB, 'G', 'E', 'T'});
    request.insert(request.end(), suffix.begin(), suffix.end());
    result = handshake.consume(request);
    QVERIFY(result);
    QCOMPARE(result->status, Socks5HandshakeStatus::DestinationReady);
    const std::optional expectedDomain = Socks5Destination{.host = "example.test", .port = 443};
    QVERIFY(result->destination == expectedDomain);
    QCOMPARE(result->remainingData, bytes({'G', 'E', 'T'}));
}

void Socks5ProtocolTests::parsesIpv4AndIpv6Destinations()
{
    const auto parse = [](const std::vector<std::byte> &request) {
        Socks5Handshake handshake;
        const auto greeting = bytes({0x05, 0x01, 0x00});
        auto method = handshake.consume(greeting);
        if (!method)
        {
            return std::optional<Socks5Destination>{};
        }
        auto result = handshake.consume(request);
        return result && result->destination ? result->destination : std::optional<Socks5Destination>{};
    };

    const std::optional expectedIpv4 = Socks5Destination{.host = "192.0.2.7", .port = 22};
    QVERIFY(parse(bytes({0x05, 0x01, 0x00, 0x01, 192, 0, 2, 7, 0, 22})) == expectedIpv4);

    const std::optional expectedIpv6 = Socks5Destination{.host = "2001:db8:0:0:0:0:0:1", .port = 8080};
    QVERIFY(
        parse(bytes({0x05, 0x01, 0x00, 0x04, 0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0x1F, 0x90}))
        == expectedIpv6);
}

void Socks5ProtocolTests::rejectsAuthenticationCommandAddressAndSizeViolations()
{
    Socks5Handshake authentication;
    auto result = authentication.consume(bytes({0x05, 0x01, 0x02}));
    QVERIFY(result);
    QCOMPARE(result->status, Socks5HandshakeStatus::Rejected);
    QCOMPARE(result->response, bytes({0x05, 0xFF}));

    Socks5Handshake command;
    QVERIFY(command.consume(bytes({0x05, 0x01, 0x00})));
    result = command.consume(bytes({0x05, 0x02, 0x00, 0x01, 127, 0, 0, 1, 0, 80}));
    QVERIFY(result);
    QCOMPARE(result->status, Socks5HandshakeStatus::Rejected);
    QCOMPARE(std::to_integer<std::uint8_t>(result->response[1]),
             static_cast<std::uint8_t>(Socks5ReplyCode::CommandNotSupported));

    Socks5Handshake address;
    QVERIFY(address.consume(bytes({0x05, 0x01, 0x00})));
    result = address.consume(bytes({0x05, 0x01, 0x00, 0x09}));
    QVERIFY(result);
    QCOMPARE(result->status, Socks5HandshakeStatus::Rejected);

    Socks5Handshake oversized;
    std::vector<std::byte> large(maximumSocks5HandshakeBytes + 1);
    const auto limited = oversized.consume(large);
    QVERIFY(!limited);
    QCOMPARE(limited.error(), Socks5ProtocolError::ResourceLimit);
}

void Socks5ProtocolTests::emitsBoundedReplies()
{
    const auto success = Socks5Handshake::reply(Socks5ReplyCode::Succeeded);
    QCOMPARE(success.size(), std::size_t{10});
    QCOMPARE(std::to_integer<std::uint8_t>(success[0]), std::uint8_t{0x05});
    QCOMPARE(std::to_integer<std::uint8_t>(success[1]), std::uint8_t{0x00});
    const auto refused = Socks5Handshake::reply(Socks5ReplyCode::ConnectionRefused);
    QCOMPARE(std::to_integer<std::uint8_t>(refused[1]), std::uint8_t{0x05});
}

} // namespace

QTEST_GUILESS_MAIN(Socks5ProtocolTests)

#include "socks5_protocol_tests.moc"
