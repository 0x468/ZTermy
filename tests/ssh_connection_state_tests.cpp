#include "domain/ssh/SshConnectionState.h"
#include "domain/ssh/SshProfile.h"

#include <QTest>

#include <array>

namespace
{

using ztermy::ssh::SshConnectionPhase;
using ztermy::ssh::SshConnectionState;
using ztermy::ssh::SshFailureKind;
using ztermy::ssh::SshStateError;

void advanceKnownHostConnection(SshConnectionState &state)
{
    QVERIFY(state.start());
    QVERIFY(state.advanceTo(SshConnectionPhase::Connecting));
    QVERIFY(state.advanceTo(SshConnectionPhase::Handshaking));
    QVERIFY(state.advanceTo(SshConnectionPhase::VerifyingHostKey));
    QVERIFY(state.advanceTo(SshConnectionPhase::Authenticating));
    QVERIFY(state.advanceTo(SshConnectionPhase::OpeningChannel));
    QVERIFY(state.advanceTo(SshConnectionPhase::Connected));
}

} // namespace

class SshConnectionStateTests final : public QObject
{
    Q_OBJECT

private slots:
    void completesKnownHostConnection();
    void pausesUnknownHostBeforeAuthentication();
    void preservesDistinctFailureKinds();
    void rejectsOutOfOrderTransitions();
    void closesAndReconnects();
    void boundsAutomaticReconnectPolicy();
};

void SshConnectionStateTests::completesKnownHostConnection()
{
    SshConnectionState state;
    advanceKnownHostConnection(state);

    QCOMPARE(state.phase(), SshConnectionPhase::Connected);
    QVERIFY(!state.failure().has_value());
}

void SshConnectionStateTests::pausesUnknownHostBeforeAuthentication()
{
    SshConnectionState state;
    QVERIFY(state.start());
    QVERIFY(state.advanceTo(SshConnectionPhase::Connecting));
    QVERIFY(state.advanceTo(SshConnectionPhase::Handshaking));
    QVERIFY(state.advanceTo(SshConnectionPhase::VerifyingHostKey));
    QVERIFY(state.advanceTo(SshConnectionPhase::AwaitingHostKeyConfirmation));

    QCOMPARE(state.phase(), SshConnectionPhase::AwaitingHostKeyConfirmation);
    QVERIFY(state.advanceTo(SshConnectionPhase::Authenticating));
}

void SshConnectionStateTests::preservesDistinctFailureKinds()
{
    constexpr std::array failureKinds = {
        SshFailureKind::NameResolutionFailed, SshFailureKind::ConnectionRefused,      SshFailureKind::TimedOut,
        SshFailureKind::HostKeyChanged,       SshFailureKind::AuthenticationRejected, SshFailureKind::RemoteClosed,
    };

    for (const SshFailureKind failureKind : failureKinds)
    {
        SshConnectionState state;
        QVERIFY(state.start());
        QVERIFY(state.fail(failureKind));
        QCOMPARE(state.phase(), SshConnectionPhase::Failed);
        QVERIFY(state.failure().has_value());
        QCOMPARE(*state.failure(), failureKind);
        QVERIFY(state.resetFailure());
        QCOMPARE(state.phase(), SshConnectionPhase::Disconnected);
        QVERIFY(!state.failure().has_value());
    }
}

void SshConnectionStateTests::rejectsOutOfOrderTransitions()
{
    SshConnectionState state;
    const auto disconnectedToConnected = state.advanceTo(SshConnectionPhase::Connected);
    QVERIFY(!disconnectedToConnected);
    QCOMPARE(disconnectedToConnected.error(), SshStateError::InvalidTransition);

    QVERIFY(state.start());
    const auto resolvingToAuthentication = state.advanceTo(SshConnectionPhase::Authenticating);
    QVERIFY(!resolvingToAuthentication);
    QCOMPARE(resolvingToAuthentication.error(), SshStateError::InvalidTransition);

    QVERIFY(state.fail(SshFailureKind::TimedOut));
    const auto failTwice = state.fail(SshFailureKind::TransportError);
    QVERIFY(!failTwice);
    QCOMPARE(failTwice.error(), SshStateError::InvalidFailure);
}

void SshConnectionStateTests::closesAndReconnects()
{
    SshConnectionState state;
    advanceKnownHostConnection(state);
    QVERIFY(state.requestClose());
    QCOMPARE(state.phase(), SshConnectionPhase::Closing);
    QVERIFY(state.completeClose());
    QCOMPARE(state.phase(), SshConnectionPhase::Disconnected);

    QVERIFY(state.start());
    QCOMPARE(state.phase(), SshConnectionPhase::Resolving);
}

void SshConnectionStateTests::boundsAutomaticReconnectPolicy()
{
    using ztermy::ssh::SshReconnectPolicy;
    using ztermy::ssh::SshSessionOptions;

    QVERIFY(ztermy::ssh::shouldReconnectAfter(SshReconnectPolicy::OnTransportFailure, SshFailureKind::TransportError));
    QVERIFY(ztermy::ssh::shouldReconnectAfter(SshReconnectPolicy::OnTransportFailure, SshFailureKind::RemoteClosed));
    QVERIFY(!ztermy::ssh::shouldReconnectAfter(SshReconnectPolicy::Never, SshFailureKind::TransportError));
    QVERIFY(!ztermy::ssh::shouldReconnectAfter(SshReconnectPolicy::OnTransportFailure,
                                               SshFailureKind::AuthenticationRejected));
    QVERIFY(!ztermy::ssh::shouldReconnectAfter(SshReconnectPolicy::OnTransportFailure, SshFailureKind::HostKeyChanged));

    SshSessionOptions options;
    options.reconnectInitialBackoffMilliseconds = 1000;
    QCOMPARE(ztermy::ssh::reconnectBackoffMilliseconds(options, 0), 0U);
    QCOMPARE(ztermy::ssh::reconnectBackoffMilliseconds(options, 1), 1000U);
    QCOMPARE(ztermy::ssh::reconnectBackoffMilliseconds(options, 2), 2000U);
    QCOMPARE(ztermy::ssh::reconnectBackoffMilliseconds(options, 6), 30000U);
    QCOMPARE(ztermy::ssh::reconnectBackoffMilliseconds(options, 10), 30000U);
}

QTEST_GUILESS_MAIN(SshConnectionStateTests)

#include "ssh_connection_state_tests.moc"
