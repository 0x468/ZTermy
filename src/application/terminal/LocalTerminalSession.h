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
#include <optional>
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
    void queuePaste(const QByteArray &bytes);
    void requestResize(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels);
    void requestScroll(int rows);
    void requestSelection(quint16 startColumn, quint16 startRow, quint16 endColumn, quint16 endRow, bool rectangular);
    void clearSelection();
    void copySelection();

signals:
    void snapshotReady(ztermy::terminal::TerminalSnapshotPtr snapshot);
    void clipboardTextReady(const QString &text);
    void statusChanged(const QString &status);
    void runningChanged(bool running);

private slots:
    void deliverLatestSnapshot();

private:
    struct InputCommand
    {
        QByteArray bytes;
    };
    struct PasteCommand
    {
        QByteArray bytes;
    };
    struct ScrollCommand
    {
        int rows = 0;
    };
    struct SelectionCommand
    {
        std::optional<TerminalSelection> selection;
    };
    struct CopyCommand
    {
    };

    using Command =
        std::variant<InputCommand, PasteCommand, TerminalGeometry, ScrollCommand, SelectionCommand, CopyCommand>;

    void queueByteCommand(Command command, std::size_t byteCount);
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
