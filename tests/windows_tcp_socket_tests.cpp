#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <array>
#include <chrono>
#include <future>
#include <span>
#include <stop_token>
#include <utility>

using namespace std::chrono_literals;

class WindowsTcpSocketTests final : public QObject
{
    Q_OBJECT

private slots:
    void connectsToLoopbackListener();
    void failsClosedLoopbackPort();
    void honorsPreRequestedCancellation();
    void rejectsInvalidEndpoint();
    void moveTransfersSocketOwnership();
    void transfersBytesThroughInterface();
    void interruptEventWakesPendingRead();
};

void WindowsTcpSocketTests::connectsToLoopbackListener()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    auto socket = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", server.serverPort(), 2s);
    if (!socket)
    {
        QFAIL(qPrintable(QStringLiteral("connect failed: kind=%1 native=%2")
                             .arg(static_cast<int>(socket.error().kind))
                             .arg(socket.error().nativeCode)));
    }

    QVERIFY(socket->valid());
    QVERIFY(server.waitForNewConnection(1000));
}

void WindowsTcpSocketTests::failsClosedLoopbackPort()
{
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const std::uint16_t unusedPort = reservation.serverPort();
    reservation.close();

    auto socket = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", unusedPort, 2s);
    QVERIFY(!socket);
    QVERIFY(socket.error().kind == ztermy::ssh::TcpConnectErrorKind::ConnectionRefused
            || socket.error().kind == ztermy::ssh::TcpConnectErrorKind::TimedOut);
}

void WindowsTcpSocketTests::honorsPreRequestedCancellation()
{
    std::stop_source stopSource;
    stopSource.request_stop();

    auto socket = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", 22, 2s, stopSource.get_token());
    QVERIFY(!socket);
    QCOMPARE(socket.error().kind, ztermy::ssh::TcpConnectErrorKind::Cancelled);
}

void WindowsTcpSocketTests::rejectsInvalidEndpoint()
{
    auto emptyHost = ztermy::ssh::WindowsTcpSocket::connect("", 22, 2s);
    QVERIFY(!emptyHost);
    QCOMPARE(emptyHost.error().kind, ztermy::ssh::TcpConnectErrorKind::InvalidEndpoint);

    auto zeroPort = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", 0, 2s);
    QVERIFY(!zeroPort);
    QCOMPARE(zeroPort.error().kind, ztermy::ssh::TcpConnectErrorKind::InvalidEndpoint);
}

void WindowsTcpSocketTests::moveTransfersSocketOwnership()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    auto socket = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", server.serverPort(), 2s);
    if (!socket)
    {
        QFAIL("loopback connection failed");
    }

    ztermy::ssh::WindowsTcpSocket moved(std::move(*socket));
    QVERIFY(moved.valid());
    QVERIFY(!socket->valid());

    moved.close();
    QVERIFY(!moved.valid());
}

void WindowsTcpSocketTests::transfersBytesThroughInterface()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    auto socket = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", server.serverPort(), 2s);
    QVERIFY(socket);
    QVERIFY(server.waitForNewConnection(1000));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    ztermy::ssh::SshByteTransport &transport = *socket;

    const QByteArray outbound = QByteArrayLiteral("transport-write");
    auto written = transport.write(std::span(outbound.constData(), static_cast<std::size_t>(outbound.size())));
    if (!written && written.error().kind == ztermy::ssh::SshByteTransportErrorKind::WouldBlock)
    {
        QVERIFY(transport.waitUntilReady(ztermy::ssh::SocketIoInterest::Write, std::chrono::steady_clock::now() + 2s));
        written = transport.write(std::span(outbound.constData(), static_cast<std::size_t>(outbound.size())));
    }
    QVERIFY(written);
    QCOMPARE(*written, static_cast<std::size_t>(outbound.size()));
    QVERIFY(peer->waitForReadyRead(1000));
    QCOMPARE(peer->readAll(), outbound);

    const QByteArray inbound = QByteArrayLiteral("transport-read");
    QCOMPARE(peer->write(inbound), inbound.size());
    QVERIFY(peer->waitForBytesWritten(1000));
    QVERIFY(transport.waitUntilReady(ztermy::ssh::SocketIoInterest::Read, std::chrono::steady_clock::now() + 2s));
    std::array<char, 64> buffer{};
    auto received = transport.read(buffer);
    QVERIFY(received);
    QCOMPARE(QByteArray(buffer.data(), static_cast<qsizetype>(*received)), inbound);
}

void WindowsTcpSocketTests::interruptEventWakesPendingRead()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    auto socket = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", server.serverPort(), 2s);
    QVERIFY(socket);
    QVERIFY(server.waitForNewConnection(1000));

    ztermy::ssh::WindowsWaitEvent interrupt;
    QVERIFY(interrupt.valid());
    const auto started = std::chrono::steady_clock::now();
    auto pending = std::async(std::launch::async, [&] {
        return socket->waitUntilReady(ztermy::ssh::SocketIoInterest::Read, std::chrono::steady_clock::now() + 5s, {},
                                      interrupt.nativeHandle());
    });

    QTest::qWait(50);
    QVERIFY(interrupt.signal());
    QCOMPARE(pending.wait_for(500ms), std::future_status::ready);
    const auto result = pending.get();
    QVERIFY(!result);
    QCOMPARE(result.error().kind, ztermy::ssh::SshByteTransportErrorKind::Cancelled);
    QVERIFY(std::chrono::steady_clock::now() - started < 500ms);
}

QTEST_GUILESS_MAIN(WindowsTcpSocketTests)

#include "windows_tcp_socket_tests.moc"
