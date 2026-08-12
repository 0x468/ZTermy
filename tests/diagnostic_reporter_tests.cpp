#include "application/diagnostics/DiagnosticReporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace
{

void writeFixture(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

ztermy::config::ApplicationPaths testPaths(const QString &root)
{
    ztermy::config::ApplicationPaths paths;
    paths.mode = ztermy::config::StorageMode::portable;
    paths.dataDirectory = root + QStringLiteral("/private-data");
    paths.localDataDirectory = root + QStringLiteral("/private-local-data");
    paths.profilesFile = paths.dataDirectory + QStringLiteral("/profiles.json");
    paths.knownHostsFile = paths.dataDirectory + QStringLiteral("/known-hosts.json");
    paths.settingsFile = paths.dataDirectory + QStringLiteral("/settings.json");
    paths.credentialsFile = paths.dataDirectory + QStringLiteral("/credentials.vault");
    paths.logsDirectory = paths.localDataDirectory + QStringLiteral("/logs");
    paths.crashDirectory = paths.localDataDirectory + QStringLiteral("/crashes");
    return paths;
}

} // namespace

class DiagnosticReporterTests final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setApplicationName(QStringLiteral("ztermy"));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.2.1-test"));
    }

    void exportsOnlyPrivacySafeMetadata()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const auto paths = testPaths(temporaryDirectory.path());
        QVERIFY(QDir().mkpath(paths.logsDirectory));
        QVERIFY(QDir().mkpath(paths.crashDirectory));

        const QByteArray logSecret{"password=do-not-export\nssh operator@secret-host\n"};
        const QByteArray dumpSecret{"terminal command and credential bytes"};
        writeFixture(paths.logsDirectory + QStringLiteral("/ztermy.log"), logSecret);
        writeFixture(paths.crashDirectory + QStringLiteral("/secret-crash.dmp"), dumpSecret);

        ztermy::diagnostics::DiagnosticReporter reporter(paths);
        reporter.setAiPrivacySummary(QJsonObject{
            {QStringLiteral("provider"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("ollama")}}},
            {QStringLiteral("diagnosticExportBoundary"), QJsonObject{{QStringLiteral("includesCredentials"), false}}}});
        const QString reportPath = temporaryDirectory.path() + QStringLiteral("/exports/diagnostic.json");
        QVERIFY(reporter.exportReport(QUrl::fromLocalFile(reportPath)));
        QVERIFY(reporter.lastError().isEmpty());

        QFile reportFile(reportPath);
        QVERIFY(reportFile.open(QIODevice::ReadOnly));
        const QByteArray serialized = reportFile.readAll();
        const QJsonDocument document = QJsonDocument::fromJson(serialized);
        QVERIFY(document.isObject());

        const QJsonObject root = document.object();
        QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 2);
        QCOMPARE(root.value(QStringLiteral("application")).toObject().value(QStringLiteral("version")).toString(),
                 QStringLiteral("0.2.1-test"));
        QCOMPARE(root.value(QStringLiteral("application")).toObject().value(QStringLiteral("storageMode")).toString(),
                 QStringLiteral("portable"));

        const QJsonObject artifacts = root.value(QStringLiteral("artifacts")).toObject();
        QCOMPARE(artifacts.value(QStringLiteral("logs")).toObject().value(QStringLiteral("fileCount")).toInt(), 1);
        QCOMPARE(artifacts.value(QStringLiteral("logs")).toObject().value(QStringLiteral("totalBytes")).toInteger(),
                 static_cast<qint64>(logSecret.size()));
        QCOMPARE(artifacts.value(QStringLiteral("crashes")).toObject().value(QStringLiteral("fileCount")).toInt(), 1);
        QCOMPARE(artifacts.value(QStringLiteral("crashes")).toObject().value(QStringLiteral("totalBytes")).toInteger(),
                 static_cast<qint64>(dumpSecret.size()));

        const QJsonObject privacy = root.value(QStringLiteral("privacy")).toObject();
        for (auto iterator = privacy.constBegin(); iterator != privacy.constEnd(); ++iterator)
        {
            QVERIFY(!iterator.value().toBool());
        }
        QCOMPARE(root.value(QStringLiteral("ai"))
                     .toObject()
                     .value(QStringLiteral("provider"))
                     .toObject()
                     .value(QStringLiteral("kind"))
                     .toString(),
                 QStringLiteral("ollama"));

        QVERIFY(!serialized.contains(logSecret));
        QVERIFY(!serialized.contains(dumpSecret));
        QVERIFY(!serialized.contains("secret-host"));
        QVERIFY(!serialized.contains("secret-crash.dmp"));
        QVERIFY(!serialized.contains(temporaryDirectory.path().toUtf8()));
    }

    void appendsJsonSuffix()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        ztermy::diagnostics::DiagnosticReporter reporter(testPaths(temporaryDirectory.path()));
        const QString reportPath = temporaryDirectory.path() + QStringLiteral("/ztermy-diagnostic");

        QVERIFY(reporter.exportReport(QUrl::fromLocalFile(reportPath)));
        QVERIFY(QFile::exists(reportPath + QStringLiteral(".json")));
    }

    void rejectsNonLocalDestination()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        ztermy::diagnostics::DiagnosticReporter reporter(testPaths(temporaryDirectory.path()));

        QVERIFY(!reporter.exportReport(QUrl{QStringLiteral("https://example.invalid/report.json")}));
        QVERIFY(!reporter.lastError().isEmpty());
    }
};

QTEST_MAIN(DiagnosticReporterTests)

#include "diagnostic_reporter_tests.moc"
