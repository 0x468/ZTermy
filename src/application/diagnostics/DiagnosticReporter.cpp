#include "application/diagnostics/DiagnosticReporter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>

#include <utility>

namespace ztermy::diagnostics
{
namespace
{

[[nodiscard]] QJsonObject directorySummary(const QString &directoryPath)
{
    qint64 totalBytes = 0;
    qsizetype fileCount = 0;
    QDateTime newestModified;
    const QFileInfoList files = QDir(directoryPath).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo &file : files)
    {
        if (!file.isFile())
        {
            continue;
        }
        ++fileCount;
        totalBytes += file.size();
        if (!newestModified.isValid() || file.lastModified() > newestModified)
        {
            newestModified = file.lastModified();
        }
    }

    QJsonObject summary{{QStringLiteral("fileCount"), fileCount}, {QStringLiteral("totalBytes"), totalBytes}};
    if (newestModified.isValid())
    {
        summary.insert(QStringLiteral("newestModifiedUtc"), newestModified.toUTC().toString(Qt::ISODateWithMs));
    }
    return summary;
}

[[nodiscard]] QJsonDocument buildReport(const config::ApplicationPaths &paths, const QJsonObject &aiPrivacySummary)
{
    const QJsonObject application{
        {QStringLiteral("name"), QCoreApplication::applicationName()},
        {QStringLiteral("version"), QCoreApplication::applicationVersion()},
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("storageMode"), config::storageModeName(paths.mode)},
    };
    const QJsonObject system{
        {QStringLiteral("productType"), QSysInfo::productType()},
        {QStringLiteral("productVersion"), QSysInfo::productVersion()},
        {QStringLiteral("kernelType"), QSysInfo::kernelType()},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("currentCpuArchitecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("buildCpuArchitecture"), QSysInfo::buildCpuArchitecture()},
    };
    const QJsonObject artifacts{
        {QStringLiteral("logs"), directorySummary(paths.logsDirectory)},
        {QStringLiteral("crashes"), directorySummary(paths.crashDirectory)},
    };
    const QJsonObject privacy{
        {QStringLiteral("includesLogContents"), false},  {QStringLiteral("includesCrashDumps"), false},
        {QStringLiteral("includesTerminalData"), false}, {QStringLiteral("includesCredentials"), false},
        {QStringLiteral("includesProfileData"), false},  {QStringLiteral("includesCommandHistory"), false},
    };
    return QJsonDocument{QJsonObject{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("generatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("application"), application},
        {QStringLiteral("system"), system},
        {QStringLiteral("artifacts"), artifacts},
        {QStringLiteral("privacy"), privacy},
        {QStringLiteral("ai"), aiPrivacySummary},
    }};
}

} // namespace

DiagnosticReporter::DiagnosticReporter(config::ApplicationPaths paths, QObject *parent)
    : QObject(parent), m_paths(std::move(paths))
{
}

QString DiagnosticReporter::lastError() const
{
    return m_lastError;
}

void DiagnosticReporter::setAiPrivacySummary(QJsonObject summary)
{
    m_aiPrivacySummary = std::move(summary);
}

bool DiagnosticReporter::exportReport(const QUrl &destination)
{
    if (!destination.isValid() || !destination.isLocalFile())
    {
        setLastError(tr("Choose a local file for the diagnostic report."));
        return false;
    }

    QString outputPath = destination.toLocalFile();
    if (outputPath.isEmpty())
    {
        setLastError(tr("Choose a local file for the diagnostic report."));
        return false;
    }
    if (QFileInfo(outputPath).suffix().isEmpty())
    {
        outputPath += QStringLiteral(".json");
    }

    const QString parentDirectory = QFileInfo(outputPath).absolutePath();
    if (parentDirectory.isEmpty() || !QDir().mkpath(parentDirectory))
    {
        setLastError(tr("Could not create the diagnostic report folder."));
        return false;
    }

    QSaveFile reportFile(outputPath);
    if (!reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        setLastError(tr("Could not open the diagnostic report for writing."));
        return false;
    }
    const QByteArray report = buildReport(m_paths, m_aiPrivacySummary).toJson(QJsonDocument::Indented);
    if (reportFile.write(report) != report.size() || !reportFile.commit())
    {
        setLastError(tr("Could not save the diagnostic report."));
        return false;
    }

    setLastError({});
    return true;
}

bool DiagnosticReporter::openLogsDirectory()
{
    return openDirectory(m_paths.logsDirectory);
}

bool DiagnosticReporter::openCrashDirectory()
{
    return openDirectory(m_paths.crashDirectory);
}

bool DiagnosticReporter::openDirectory(const QString &directory)
{
    if (directory.isEmpty() || !QDir().mkpath(directory))
    {
        setLastError(tr("Could not create the diagnostic data folder."));
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
    {
        setLastError(tr("Windows could not open the diagnostic data folder."));
        return false;
    }

    setLastError({});
    return true;
}

void DiagnosticReporter::setLastError(QString error)
{
    if (m_lastError == error)
    {
        return;
    }
    m_lastError = std::move(error);
    emit lastErrorChanged();
}

} // namespace ztermy::diagnostics
