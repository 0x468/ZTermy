#pragma once

#include "core/diagnostics/LatencyHistogram.h"
#include "domain/terminal/TerminalEngine.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
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

class LocalTerminalSessionBackend : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~LocalTerminalSessionBackend() override = default;

    LocalTerminalSessionBackend(const LocalTerminalSessionBackend &) = delete;
    LocalTerminalSessionBackend &operator=(const LocalTerminalSessionBackend &) = delete;

    [[nodiscard]] virtual std::error_code start(TerminalGeometry geometry) = 0;
    virtual void stop() noexcept = 0;

public slots:
    virtual void queueInput(const QByteArray &bytes) = 0;
    virtual void queuePaste(const QByteArray &bytes) = 0;
    virtual void requestResize(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels) = 0;
    virtual void requestScroll(int rows) = 0;
    virtual void requestSelection(quint16 startColumn, quint16 startRow, quint16 endColumn, quint16 endRow,
                                  bool rectangular) = 0;
    virtual void clearSelection() = 0;
    virtual void copySelection() = 0;
    virtual void search(const QString &query, bool backwards, bool caseSensitive) = 0;
    virtual void clearSearch() = 0;

signals:
    void snapshotReady(ztermy::terminal::TerminalSnapshotPtr snapshot);
    void clipboardTextReady(const QString &text);
    void statusChanged(const QString &status);
    void runningChanged(bool running);
    void searchResultReady(const QString &query, quint32 current, quint32 total, bool wrapped);
};

class LocalTerminalSession final : public LocalTerminalSessionBackend
{
    Q_OBJECT

public:
    explicit LocalTerminalSession(QObject *parent = nullptr);
    ~LocalTerminalSession() override;

    LocalTerminalSession(const LocalTerminalSession &) = delete;
    LocalTerminalSession &operator=(const LocalTerminalSession &) = delete;

    [[nodiscard]] std::error_code start(TerminalGeometry geometry) override;
    void stop() noexcept override;
    [[nodiscard]] diagnostics::LatencySummary inputQueueLatencySummary() const noexcept;

public slots:
    void queueInput(const QByteArray &bytes) override;
    void queuePaste(const QByteArray &bytes) override;
    void requestResize(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels) override;
    void requestScroll(int rows) override;
    void requestSelection(quint16 startColumn, quint16 startRow, quint16 endColumn, quint16 endRow,
                          bool rectangular) override;
    void clearSelection() override;
    void copySelection() override;
    void search(const QString &query, bool backwards, bool caseSensitive) override;
    void clearSearch() override;

private slots:
    void deliverLatestSnapshot();

private:
    struct InputCommand
    {
        QByteArray bytes;
        std::chrono::steady_clock::time_point enqueuedAt;
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
    struct SearchCommand
    {
        QByteArray query;
        TerminalSearchDirection direction = TerminalSearchDirection::forward;
        bool caseSensitive = false;
    };
    struct ClearSearchCommand
    {
    };

    using Command = std::variant<InputCommand, PasteCommand, TerminalGeometry, ScrollCommand, SelectionCommand,
                                 CopyCommand, SearchCommand, ClearSearchCommand>;

    void queueByteCommand(Command command, std::size_t byteCount);
    void readLoop(const std::stop_token &stopToken);
    void writeLoop(const std::stop_token &stopToken);
    void publishSnapshot();
    void postStatus(const QString &status);
    void resetMetrics() noexcept;
    void logMetrics() const;

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
    std::atomic_uint64_t m_readBytes = 0;
    std::atomic_uint64_t m_snapshotsProduced = 0;
    std::atomic_uint64_t m_snapshotsDelivered = 0;
    std::atomic_uint64_t m_snapshotsCoalesced = 0;
    std::atomic_uint64_t m_fullDamageSnapshots = 0;
    std::atomic_uint64_t m_partialDamageSnapshots = 0;
    std::atomic_uint64_t m_cleanSnapshots = 0;
    std::atomic_uint64_t m_snapshotBuildNanoseconds = 0;
    std::atomic_uint64_t m_maxSnapshotBuildNanoseconds = 0;
    diagnostics::LatencyHistogram m_inputQueueLatency;
};

} // namespace ztermy::terminal

Q_DECLARE_METATYPE(ztermy::terminal::TerminalSnapshotPtr)
