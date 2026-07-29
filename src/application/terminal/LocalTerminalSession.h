#pragma once

#include "domain/terminal/TerminalEngine.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <variant>

namespace ztermy::terminal
{

class ConPtyProcess;
class GhosttyTerminalEngine;

class LocalTerminalSession final : public QObject
{
    Q_OBJECT

public:
    explicit LocalTerminalSession(QObject *parent = nullptr);
    ~LocalTerminalSession() override;

    LocalTerminalSession(const LocalTerminalSession &) = delete;
    LocalTerminalSession &operator=(const LocalTerminalSession &) = delete;

    [[nodiscard]] std::error_code start(TerminalGeometry geometry);
    void stop() noexcept;

public slots:
    void queueInput(const QByteArray &bytes);
    void requestResize(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels);

signals:
    void snapshotReady(ztermy::terminal::TerminalSnapshotPtr snapshot);
    void statusChanged(const QString &status);
    void runningChanged(bool running);

private slots:
    void deliverLatestSnapshot();

private:
    using Command = std::variant<QByteArray, TerminalGeometry>;

    void readLoop(const std::stop_token &stopToken);
    void writeLoop(const std::stop_token &stopToken);
    void publishSnapshot();
    void postStatus(const QString &status);

    static constexpr std::size_t maximumQueuedInputBytes = 1024U * 1024U;

    std::unique_ptr<ConPtyProcess> m_process;
    std::unique_ptr<GhosttyTerminalEngine> m_engine;
    std::jthread m_readThread;
    std::jthread m_writeThread;

    std::mutex m_engineMutex;
    std::mutex m_commandMutex;
    std::condition_variable_any m_commandAvailable;
    std::deque<Command> m_commands;
    std::size_t m_queuedInputBytes = 0;

    std::mutex m_snapshotMutex;
    TerminalSnapshotPtr m_pendingSnapshot;
    std::atomic_bool m_snapshotDeliveryScheduled = false;
    std::atomic_bool m_running = false;
};

} // namespace ztermy::terminal

Q_DECLARE_METATYPE(ztermy::terminal::TerminalSnapshotPtr)
