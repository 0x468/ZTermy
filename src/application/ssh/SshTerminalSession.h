#pragma once

#include "core/diagnostics/LatencyHistogram.h"
#include "core/security/SensitiveByteArray.h"
#include "domain/ssh/SshConnectionState.h"
#include "domain/ssh/SshProfile.h"
#include "domain/terminal/TerminalEngine.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

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

struct SshConnectionRequest final
{
    QString host;
    std::uint16_t port = 22;
    QString username;
    SshAuthenticationMethod authentication = SshAuthenticationMethod::PrivateKey;
    QString privateKeyPath;
    security::SensitiveByteArray secret;
    QString knownHostsPath;
};

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
    void requestShellHistory(quint64 requestId);

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
    struct HistoryCommand final
    {
        quint64 requestId = 0;
    };

    enum class HostKeyDecision : std::uint8_t
    {
        Pending,
        AcceptOnce,
        AcceptAndRemember,
        Reject,
    };

    using Command = std::variant<InputCommand, PasteCommand, terminal::TerminalGeometry, ScrollCommand,
                                 SelectionCommand, CopyCommand, SearchCommand, ClearSearchCommand, HistoryCommand>;

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
    void finishWorker(const QString &status, SshConnectionPhase phase);
    void signalCommandWake() noexcept;
    void resetMetrics() noexcept;
    void logMetrics() const;

    static constexpr std::size_t maximumQueuedInputBytes = std::size_t{1024} * 1024;

    std::unique_ptr<terminal::GhosttyTerminalEngine> m_engine;
    std::jthread m_worker;

    std::mutex m_commandMutex;
    std::deque<Command> m_commands;
    std::size_t m_queuedInputBytes = 0;
    WindowsWaitEvent m_commandWakeEvent;

    std::mutex m_hostKeyMutex;
    std::condition_variable_any m_hostKeyAvailable;
    HostKeyDecision m_hostKeyDecision = HostKeyDecision::Pending;
    bool m_awaitingHostKey = false;

    std::mutex m_snapshotMutex;
    terminal::TerminalSnapshotPtr m_pendingSnapshot;
    std::atomic_bool m_snapshotDeliveryScheduled = false;
    std::atomic_bool m_running = false;
    diagnostics::LatencyHistogram m_inputQueueLatency;
};

} // namespace ztermy::ssh

Q_DECLARE_METATYPE(ztermy::ssh::SshConnectionPhase)
