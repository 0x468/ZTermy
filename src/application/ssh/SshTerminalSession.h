#pragma once

#include "application/ssh/SshConnectionBootstrap.h"
#include "application/ssh/SshConnectionRequest.h"
#include "core/diagnostics/LatencyHistogram.h"
#include "domain/ssh/SshConnectionState.h"
#include "domain/telemetry/RemoteTelemetry.h"
#include "domain/terminal/TerminalEngine.h"
#include "domain/terminal/TerminalOutputSink.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"
#include "platform/windows/WindowsTerminalTextCodec.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <variant>

namespace ztermy::terminal
{
class GhosttyTerminalEngine;
}

namespace ztermy::ssh
{

[[nodiscard]] QString sshFailureStatus(SshFailureKind failure);

class SshTerminalSession final : public QObject
{
    Q_OBJECT

public:
    explicit SshTerminalSession(QObject *parent = nullptr);
    ~SshTerminalSession() override;

    SshTerminalSession(const SshTerminalSession &) = delete;
    SshTerminalSession &operator=(const SshTerminalSession &) = delete;

    [[nodiscard]] std::error_code start(SshConnectionRequest request, terminal::TerminalGeometry geometry);
    void stop() noexcept;
    void setOutputSink(const std::shared_ptr<terminal::TerminalOutputSink> &sink);
    [[nodiscard]] diagnostics::LatencySummary inputQueueLatencySummary() const noexcept;

public slots:
    void confirmHostKey(bool remember);
    void rejectHostKey();
    void queueInput(const QByteArray &bytes);
    void queuePaste(const QByteArray &bytes);
    void requestResize(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels);
    void requestScroll(int rows);
    void requestSelection(quint16 startColumn, quint16 startRow, quint16 endColumn, quint16 endRow, bool rectangular);
    void clearSelection();
    void copySelection();
    void search(const QString &query, bool backwards, bool caseSensitive);
    void clearSearch();
    void setEncoding(const QString &encoding);
    void requestShellHistory(quint64 requestId);
    void setRemoteTelemetryVisible(bool visible);
    void refreshRemoteTelemetry();

signals:
    void snapshotReady(ztermy::terminal::TerminalSnapshotPtr snapshot);
    void clipboardTextReady(const QString &text);
    void statusChanged(const QString &status);
    void runningChanged(bool running);
    void phaseChanged(ztermy::ssh::SshConnectionPhase phase);
    void failureOccurred(ztermy::ssh::SshFailureKind failure);
    void hostKeyConfirmationRequired(const QString &algorithm, const QString &fingerprint);
    void hostKeyChanged(const QString &algorithm, const QString &fingerprint);
    void searchResultReady(const QString &query, quint32 current, quint32 total, bool wrapped);
    void shellHistoryReady(quint64 requestId, const QString &shell, const QByteArray &contents, const QString &error);
    void remoteTelemetryReady(const ztermy::telemetry::Sample &sample);
    void remoteTelemetryStateChanged(const QString &state);

private slots:
    void deliverLatestSnapshot();
    void deliverStatus(const QString &status);
    void deliverPhase(ztermy::ssh::SshConnectionPhase phase);
    void deliverFailure(ztermy::ssh::SshFailureKind failure);
    void deliverRunning(bool running);
    void deliverHostKeyConfirmation(const QString &algorithm, const QString &fingerprint);
    void deliverHostKeyChange(const QString &algorithm, const QString &fingerprint);
    void deliverClipboardText(const QString &text);
    void deliverSearchResult(const QString &query, quint32 current, quint32 total, bool wrapped);
    void deliverShellHistory(quint64 requestId, const QString &shell, const QByteArray &contents, const QString &error);
    void deliverRemoteTelemetry(const ztermy::telemetry::Sample &sample);
    void deliverRemoteTelemetryState(const QString &state);

private:
    struct InputCommand final
    {
        QByteArray bytes;
        std::chrono::steady_clock::time_point enqueuedAt;
    };
    struct PasteCommand final
    {
        QByteArray bytes;
    };
    struct ScrollCommand final
    {
        int rows = 0;
    };
    struct SelectionCommand final
    {
        std::optional<terminal::TerminalSelection> selection;
    };
    struct CopyCommand final
    {
    };
    struct SearchCommand final
    {
        QByteArray query;
        terminal::TerminalSearchDirection direction = terminal::TerminalSearchDirection::forward;
        bool caseSensitive = false;
    };
    struct ClearSearchCommand final
    {
    };
    struct EncodingCommand final
    {
        terminal::TerminalEncoding encoding = terminal::TerminalEncoding::Utf8;
    };
    struct HistoryCommand final
    {
        quint64 requestId = 0;
    };
    struct TelemetryVisibilityCommand final
    {
        bool visible = false;
    };
    struct TelemetryRefreshCommand final
    {
    };

    using Command = std::variant<InputCommand, PasteCommand, terminal::TerminalGeometry, ScrollCommand,
                                 SelectionCommand, CopyCommand, SearchCommand, ClearSearchCommand, EncodingCommand,
                                 HistoryCommand, TelemetryVisibilityCommand, TelemetryRefreshCommand>;

    void queueByteCommand(Command command, std::size_t byteCount);
    void run(SshConnectionRequest &request, terminal::TerminalGeometry geometry, const std::stop_token &stopToken);
    void publishSnapshot();
    void postStatus(const QString &status);
    void postPhase(SshConnectionPhase phase);
    void postFailure(SshFailureKind failure);
    void postRunning(bool running);
    void postHostKeyConfirmation(const QString &algorithm, const QString &fingerprint);
    void postHostKeyChange(const QString &algorithm, const QString &fingerprint);
    void postClipboardText(const QString &text);
    void postSearchResult(const QString &query, quint32 current, quint32 total, bool wrapped);
    void postShellHistory(quint64 requestId, const QString &shell, const QByteArray &contents, const QString &error);
    void postRemoteTelemetry(const telemetry::Sample &sample);
    void postRemoteTelemetryState(const QString &state);
    void finishWorker(const QString &status, SshConnectionPhase phase);
    void signalCommandWake() noexcept;
    void resetMetrics() noexcept;
    void logMetrics() const;

    static constexpr std::size_t maximumQueuedInputBytes = std::size_t{1024} * 1024;

    std::unique_ptr<terminal::GhosttyTerminalEngine> m_engine;
    std::shared_ptr<terminal::TerminalOutputSink> m_outputSink;
    std::jthread m_worker;

    std::mutex m_commandMutex;
    std::deque<Command> m_commands;
    std::size_t m_queuedInputBytes = 0;
    WindowsWaitEvent m_commandWakeEvent;

    std::mutex m_hostKeyMutex;
    std::condition_variable_any m_hostKeyAvailable;
    std::optional<UnknownHostKeyDecision> m_hostKeyDecision;
    bool m_awaitingHostKey = false;

    std::mutex m_snapshotMutex;
    terminal::TerminalSnapshotPtr m_pendingSnapshot;
    std::atomic_bool m_snapshotDeliveryScheduled = false;
    std::atomic_bool m_running = false;
    std::atomic_bool m_telemetryRequestedVisible = false;
    diagnostics::LatencyHistogram m_inputQueueLatency;
};

} // namespace ztermy::ssh

Q_DECLARE_METATYPE(ztermy::ssh::SshConnectionPhase)
Q_DECLARE_METATYPE(ztermy::telemetry::Sample)
