#include "core/logging/Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGlobalStatic>
#include <QLoggingCategory>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <cstdlib>

namespace
{

constexpr qint64 kMaximumLogSize = qint64{4} * 1024 * 1024;

struct LogState
{
    QMutex mutex;
    QFile file;
    QtMessageHandler previousMessageHandler = nullptr;
    QString filePath;
};

Q_GLOBAL_STATIC(LogState, logState)

[[nodiscard]] QString messageTypeName(const QtMsgType type)
{
    switch (type)
    {
        case QtDebugMsg:
            return QStringLiteral("DEBUG");
        case QtInfoMsg:
            return QStringLiteral("INFO ");
        case QtWarningMsg:
            return QStringLiteral("WARN ");
        case QtCriticalMsg:
            return QStringLiteral("ERROR");
        case QtFatalMsg:
            return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKWN");
}

void writeMessage(const QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    LogState *state = logState();
    if (state == nullptr)
    {
        return;
    }

    {
        const QMutexLocker locker(&state->mutex);
        if (state->file.isOpen())
        {
            QTextStream stream(&state->file);
            stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " " << messageTypeName(type) << " ["
                   << context.category << "] [thread:" << reinterpret_cast<quintptr>(QThread::currentThreadId()) << "] "
                   << message << "\n";
            stream.flush();
            state->file.flush();
        }
    }

    if (state->previousMessageHandler != nullptr)
    {
        state->previousMessageHandler(type, context, message);
    }

    if (type == QtFatalMsg)
    {
        std::abort();
    }
}

} // namespace

namespace ztermy::logging
{

void initialize(const QString &logsDirectory)
{
#ifdef ZTERMY_DEBUG_BUILD
    QLoggingCategory::setFilterRules(QStringLiteral("ztermy.*.debug=true\n"
                                                    "ztermy.*.info=true"));
#else
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n"
                                                    "ztermy.*.info=true"));
#endif

    const QString resolvedLogsDirectory =
        logsDirectory.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs")
            : logsDirectory;
    QDir().mkpath(resolvedLogsDirectory);

    LogState *state = logState();
    state->filePath = QDir(resolvedLogsDirectory).filePath(QStringLiteral("ztermy.log"));
    state->file.setFileName(state->filePath);

    if (QFileInfo(state->filePath).size() > kMaximumLogSize)
    {
        QFile::remove(state->filePath + QStringLiteral(".old"));
        QFile::rename(state->filePath, state->filePath + QStringLiteral(".old"));
    }

    state->file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    state->previousMessageHandler = qInstallMessageHandler(writeMessage);

    qInfo().noquote() << "----- ztermy session started -----";
    qInfo().noquote() << "version=" << QCoreApplication::applicationVersion() << "logFile=" << state->filePath;
}

QString logFilePath()
{
    const LogState *state = logState();
    return state == nullptr ? QString{} : state->filePath;
}

} // namespace ztermy::logging
