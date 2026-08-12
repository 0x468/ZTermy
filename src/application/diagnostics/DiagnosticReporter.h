#pragma once

#include "core/config/ApplicationPaths.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

namespace ztermy::diagnostics
{

class DiagnosticReporter final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit DiagnosticReporter(config::ApplicationPaths paths, QObject *parent = nullptr);

    [[nodiscard]] QString lastError() const;
    void setAiPrivacySummary(QJsonObject summary);

    Q_INVOKABLE bool exportReport(const QUrl &destination);
    Q_INVOKABLE bool openLogsDirectory();
    Q_INVOKABLE bool openCrashDirectory();

signals:
    void lastErrorChanged();

private:
    [[nodiscard]] bool openDirectory(const QString &directory);
    void setLastError(QString error);

    config::ApplicationPaths m_paths;
    QJsonObject m_aiPrivacySummary;
    QString m_lastError;
};

} // namespace ztermy::diagnostics
