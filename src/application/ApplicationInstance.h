#pragma once

#include "core/config/ApplicationLaunchRequest.h"

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QPointer>
#include <QWindow>

#include <cstdint>
#include <memory>

namespace ztermy
{
class ApplicationInstance final : public QObject
{
    Q_OBJECT
public:
    enum class Result : std::uint8_t
    {
        Primary,
        Existing,
        Error
    };
    explicit ApplicationInstance(QObject *parent = nullptr);
    ~ApplicationInstance() override { release(); }
    [[nodiscard]] Result claim(const QString &dataDirectory, const config::ApplicationLaunchRequest &request = {});
    [[nodiscard]] Result claimConfigured(const QString &dataDirectory, const QString &settingsFile,
                                         const config::ApplicationLaunchRequest &request = {});
    void setWindow(QWindow *window);
    void setReady();
    void requestRestart();
    [[nodiscard]] int finish(int exitCode);
    void release();
signals:
    void activationRequested();
    void directoryOpenRequested(const QString &directory, const QString &shellId);
    void launchRequested(const ztermy::config::ApplicationLaunchRequest &request);

private:
    QLocalServer m_server;
    std::unique_ptr<QLockFile> m_lock;
    bool m_restartRequested = false;
    bool m_ready = false;
    QList<config::ApplicationLaunchRequest> m_pendingRequests;
    void dispatch(const config::ApplicationLaunchRequest &request);
};
} // namespace ztermy
