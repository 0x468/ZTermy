#pragma once

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
    [[nodiscard]] Result claim(const QString &dataDirectory);
    [[nodiscard]] Result claimConfigured(const QString &dataDirectory, const QString &settingsFile);
    void setWindow(QWindow *window);
    void requestRestart();
    [[nodiscard]] int finish(int exitCode);
    void release();
signals:
    void activationRequested();

private:
    QLocalServer m_server;
    std::unique_ptr<QLockFile> m_lock;
    bool m_restartRequested = false;
};
} // namespace ztermy
