#include "infrastructure/ssh/Libssh2Runtime.h"

#include <QTest>

class SshRuntimeTests final : public QObject
{
    Q_OBJECT

private slots:
    void initializesPinnedLibrary();
    void canRestartAfterShutdown();
};

void SshRuntimeTests::initializesPinnedLibrary()
{
    auto runtime = ztermy::ssh::Libssh2Runtime::create();
    if (!runtime)
    {
        QFAIL(runtime.error().message().c_str());
    }
    QCOMPARE(QString::fromUtf8((*runtime)->version()), QStringLiteral("1.11.1"));
    QVERIFY((*runtime)->usesOpenSsl());
}

void SshRuntimeTests::canRestartAfterShutdown()
{
    {
        auto first = ztermy::ssh::Libssh2Runtime::create();
        if (!first)
        {
            QFAIL(first.error().message().c_str());
        }
    }

    auto second = ztermy::ssh::Libssh2Runtime::create();
    if (!second)
    {
        QFAIL(second.error().message().c_str());
    }
}

QTEST_GUILESS_MAIN(SshRuntimeTests)

#include "ssh_runtime_tests.moc"
