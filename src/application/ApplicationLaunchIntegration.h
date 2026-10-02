#pragma once

#include "application/AppController.h"
#include "application/ApplicationInstance.h"
#include "core/config/ApplicationPaths.h"
#include "platform/windows/ExplorerMenuSnapshot.h"
#include "platform/windows/LaunchFeedback.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QThreadPool>

namespace ztermy
{
inline void prepareAutomatedLaunchEnvironment(const QStringList &arguments)
{
    for (const QString &argument : arguments)
        if (argument.endsWith(QStringLiteral("-smoke")) || argument == QStringLiteral("--smoke-test")
            || argument.endsWith(QStringLiteral("-benchmark")))
            qputenv("ZTERMY_TEST_ISOLATED_SHELLS", "1");
}

[[nodiscard]] inline bool showRequestedLaunchHelp(const QStringList &arguments)
{
    if (!arguments.contains(QStringLiteral("--help")))
        return false;
    windowing::showLaunchFeedback(config::ApplicationLaunchRequest::helpText());
    return true;
}

// Lifetime-bound launch routing and installed-menu snapshot publication.
class ApplicationLaunchIntegration final : public QObject
{
public:
    ApplicationLaunchIntegration(AppController &controller, ApplicationInstance &instance,
                                 const config::ApplicationPaths &paths, const QString &executable)
        : m_controller(controller)
    {
        m_writer.setMaxThreadCount(1);
        if (paths.mode == config::StorageMode::installed)
            m_snapshotPath = QString::fromStdWString(explorer::snapshotPath(executable.toStdWString()));
        connect(&instance, &ApplicationInstance::launchRequested, this,
                [&controller](const config::ApplicationLaunchRequest &request) {
                    (void)controller.openLaunchRequest(request);
                });
        connect(&controller, &AppController::applicationSettingsChanged, this,
                &ApplicationLaunchIntegration::updateExplorerMenu);
        updateExplorerMenu();
    }

private:
    void updateExplorerMenu()
    {
        if (m_snapshotPath.isEmpty())
            return;
        m_writer.start([path = m_snapshotPath, bytes = m_controller.explorerMenuConfiguration()] {
            if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                return;
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                qWarning() << "Unable to update Explorer menu preferences";
        });
    }

    AppController &m_controller;
    QString m_snapshotPath;
    QThreadPool m_writer;
};
} // namespace ztermy
