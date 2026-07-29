#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTest>

#include <chrono>
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

QTEST_GUILESS_MAIN(WindowsTcpSocketTests)

#include "windows_tcp_socket_tests.moc"
