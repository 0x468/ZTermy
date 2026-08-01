#include "core/config/ApplicationPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class ApplicationPathsTests final : public QObject
{
    Q_OBJECT

private slots:
    void resolvesInstalledDirectories();
    void resolvesPortableArgumentAndMarker();
    void resolvesCustomDirectory();
    void rejectsInvalidCustomDirectoryArguments();
    void preparesRequiredDirectories();
};

void ApplicationPathsTests::resolvesInstalledDirectories()
{
    const auto paths = ztermy::config::resolveApplicationPaths(
        {QStringLiteral("ztermy.exe")}, QStringLiteral(R"(C:\Program Files\ztermy)"), QStringLiteral(R"(C:\work)"),
        QStringLiteral(R"(C:\Users\person\AppData\Roaming\ztermy)"),
        QStringLiteral(R"(C:\Users\person\AppData\Local\ztermy)"), false);

    QVERIFY(paths.has_value());
    QCOMPARE(paths->mode, ztermy::config::StorageMode::installed);
    QCOMPARE(QDir::toNativeSeparators(paths->profilesFile),
             QStringLiteral(R"(C:\Users\person\AppData\Roaming\ztermy\profiles.json)"));
    QCOMPARE(QDir::toNativeSeparators(paths->credentialsFile),
             QStringLiteral(R"(C:\Users\person\AppData\Roaming\ztermy\credentials.zvlt)"));
    QCOMPARE(QDir::toNativeSeparators(paths->logsDirectory),
             QStringLiteral(R"(C:\Users\person\AppData\Local\ztermy\logs)"));
}

void ApplicationPathsTests::resolvesPortableArgumentAndMarker()
{
    const QString executableDirectory = QStringLiteral(R"(D:\apps\ztermy)");
    const auto argumentPaths = ztermy::config::resolveApplicationPaths(
        {QStringLiteral("ztermy.exe"), QStringLiteral("--portable")}, executableDirectory, QStringLiteral(R"(D:\work)"),
        QStringLiteral(R"(C:\roaming)"), QStringLiteral(R"(C:\local)"), false);
    const auto markerPaths = ztermy::config::resolveApplicationPaths(
        {QStringLiteral("ztermy.exe")}, executableDirectory, QStringLiteral(R"(D:\work)"),
        QStringLiteral(R"(C:\roaming)"), QStringLiteral(R"(C:\local)"), true);

    QVERIFY(argumentPaths.has_value());
    QVERIFY(markerPaths.has_value());
    QCOMPARE(argumentPaths->mode, ztermy::config::StorageMode::portable);
    QCOMPARE(markerPaths->mode, ztermy::config::StorageMode::portable);
    QCOMPARE(QDir::toNativeSeparators(argumentPaths->dataDirectory), QStringLiteral(R"(D:\apps\ztermy\data)"));
    QCOMPARE(argumentPaths->dataDirectory, markerPaths->dataDirectory);
    QCOMPARE(argumentPaths->credentialsFile,
             QDir(argumentPaths->dataDirectory).filePath(QStringLiteral("credentials.zvlt")));
    QCOMPARE(argumentPaths->crashDirectory, QDir(argumentPaths->dataDirectory).filePath(QStringLiteral("crashes")));
}

void ApplicationPathsTests::resolvesCustomDirectory()
{
    const auto paths = ztermy::config::resolveApplicationPaths(
        {QStringLiteral("ztermy.exe"), QStringLiteral("--data-dir"), QStringLiteral("state")},
        QStringLiteral(R"(D:\apps\ztermy)"), QStringLiteral(R"(D:\work)"), QStringLiteral(R"(C:\roaming)"),
        QStringLiteral(R"(C:\local)"), false);

    QVERIFY(paths.has_value());
    QCOMPARE(paths->mode, ztermy::config::StorageMode::custom);
    QCOMPARE(QDir::toNativeSeparators(paths->dataDirectory), QStringLiteral(R"(D:\work\state)"));
    QCOMPARE(paths->dataDirectory, paths->localDataDirectory);
}

void ApplicationPathsTests::rejectsInvalidCustomDirectoryArguments()
{
    const auto missing = ztermy::config::resolveApplicationPaths(
        {QStringLiteral("ztermy.exe"), QStringLiteral("--data-dir")}, QStringLiteral(R"(D:\apps\ztermy)"),
        QStringLiteral(R"(D:\work)"), QStringLiteral(R"(C:\roaming)"), QStringLiteral(R"(C:\local)"), false);
    const auto conflicting = ztermy::config::resolveApplicationPaths(
        {QStringLiteral("ztermy.exe"), QStringLiteral("--data-dir=one"), QStringLiteral("--data-dir=two")},
        QStringLiteral(R"(D:\apps\ztermy)"), QStringLiteral(R"(D:\work)"), QStringLiteral(R"(C:\roaming)"),
        QStringLiteral(R"(C:\local)"), false);

    QVERIFY(!missing.has_value());
    QVERIFY(!conflicting.has_value());
}

void ApplicationPathsTests::preparesRequiredDirectories()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir(temporary.path()).filePath(QStringLiteral("nested/state"));
    const auto paths = ztermy::config::resolveApplicationPaths(
        {QStringLiteral("ztermy.exe"), QStringLiteral("--data-dir"), root}, temporary.path(), temporary.path(),
        temporary.path(), temporary.path(), false);

    QVERIFY(paths.has_value());
    QVERIFY(ztermy::config::prepareApplicationPaths(*paths).has_value());
    QVERIFY(QFileInfo(paths->dataDirectory).isDir());
    QVERIFY(QFileInfo(paths->logsDirectory).isDir());
    QVERIFY(QFileInfo(paths->crashDirectory).isDir());
}

QTEST_APPLESS_MAIN(ApplicationPathsTests)

#include "application_paths_tests.moc"
