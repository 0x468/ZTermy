#include "domain/ssh/SshTarget.h"

#include <QTest>

class SshTargetTests final : public QObject
{
    Q_OBJECT

private slots:
    void parsesSupportedTargets();
    void rejectsAmbiguousOrInvalidTargets();
};

void SshTargetTests::parsesSupportedTargets()
{
    const auto basic = ztermy::ssh::parseSshTarget("alice@example.test");
    QVERIFY(basic);
    QCOMPARE(basic->username, std::string{"alice"});
    QCOMPARE(basic->host, std::string{"example.test"});
    QCOMPARE(basic->port, std::uint16_t{22});

    const auto customPort = ztermy::ssh::parseSshTarget(" alice@192.0.2.10:2222 ");
    QVERIFY(customPort);
    QCOMPARE(customPort->port, std::uint16_t{2222});

    const auto ipv6 = ztermy::ssh::parseSshTarget("root@[2001:db8::10]:2200");
    QVERIFY(ipv6);
    QCOMPARE(ipv6->host, std::string{"2001:db8::10"});
    QCOMPARE(ipv6->port, std::uint16_t{2200});
}

void SshTargetTests::rejectsAmbiguousOrInvalidTargets()
{
    using enum ztermy::ssh::SshTargetError;

    QCOMPARE(ztermy::ssh::parseSshTarget("host").error(), InvalidFormat);
    QCOMPARE(ztermy::ssh::parseSshTarget("@host").error(), MissingUsername);
    QCOMPARE(ztermy::ssh::parseSshTarget("user@").error(), MissingHost);
    QCOMPARE(ztermy::ssh::parseSshTarget("user@host:0").error(), InvalidPort);
    QCOMPARE(ztermy::ssh::parseSshTarget("user@host:65536").error(), InvalidPort);
    QCOMPARE(ztermy::ssh::parseSshTarget("user@2001:db8::10").error(), BracketsRequired);
    QCOMPARE(ztermy::ssh::parseSshTarget("user name@host").error(), InvalidFormat);
    QCOMPARE(ztermy::ssh::parseSshTarget("user@@host").error(), InvalidFormat);
    QCOMPARE(ztermy::ssh::parseSshTarget(std::string(257, 'u') + "@host").error(), InvalidFormat);
    QCOMPARE(ztermy::ssh::parseSshTarget("user@" + std::string(1025, 'h')).error(), InvalidFormat);
}

QTEST_GUILESS_MAIN(SshTargetTests)

#include "ssh_target_tests.moc"
