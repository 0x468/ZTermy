#include "infrastructure/ssh/OpenSshConfigImporter.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class OpenSshConfigImporterTests final : public QObject
{
    Q_OBJECT

private slots:
    void importsExactHostsAndIncludedFiles();
    void skipsWildcardHostsAndRejectsMissingFiles();
};

void OpenSshConfigImporterTests::importsExactHostsAndIncludedFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("conf.d"))));
    QFile included(directory.filePath(QStringLiteral("conf.d/lab.conf")));
    QVERIFY(included.open(QIODevice::WriteOnly));
    included.write("Host gateway\n  HostName 192.168.1.22\n  User root\n  Port 2222\n");
    included.close();
    QFile root(directory.filePath(QStringLiteral("config")));
    QVERIFY(root.open(QIODevice::WriteOnly));
    root.write("Include conf.d/*.conf\nHost app\n HostName app.example.test\n User deploy\n IdentityFile "
               "~/.ssh/id_ed25519\n ProxyJump gateway\n");
    root.close();

    const auto imported = ztermy::ssh::loadOpenSshConfig(root.fileName());
    QVERIFY(imported);
    QCOMPARE(imported->parsedFiles, std::size_t{2});
    QCOMPARE(imported->hosts.size(), std::size_t{2});
    const auto app = std::ranges::find(imported->hosts, QStringLiteral("app"), &ztermy::ssh::OpenSshHostConfig::alias);
    QVERIFY(app != imported->hosts.end());
    QCOMPARE(app->hostName, QStringLiteral("app.example.test"));
    QCOMPARE(app->user, QStringLiteral("deploy"));
    QCOMPARE(app->proxyJump, QStringLiteral("gateway"));
    QVERIFY(QFileInfo(app->identityFile).isAbsolute());
}

void OpenSshConfigImporterTests::skipsWildcardHostsAndRejectsMissingFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("config")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Host * !blocked *.example.test\n User ignored\nHost exact\n HostName 192.0.2.4\n");
    file.close();
    const auto imported = ztermy::ssh::loadOpenSshConfig(file.fileName());
    QVERIFY(imported);
    QCOMPARE(imported->hosts.size(), std::size_t{1});
    QCOMPARE(imported->hosts.front().alias, QStringLiteral("exact"));
    QVERIFY(!ztermy::ssh::loadOpenSshConfig(directory.filePath(QStringLiteral("missing"))));
}

QTEST_GUILESS_MAIN(OpenSshConfigImporterTests)

#include "open_ssh_config_importer_tests.moc"
