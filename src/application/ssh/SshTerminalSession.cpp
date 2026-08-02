#include "application/ssh/SshTerminalSession.h"

#include "domain/ssh/SshHostKey.h"
#include "domain/terminal/GhosttyTerminalEngine.h"
#include "infrastructure/ssh/KnownHostsStore.h"
#include "infrastructure/ssh/Libssh2Session.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QThread>

#include <array>
#include <chrono>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

Q_LOGGING_CATEGORY(sshSessionLog, "ztermy.ssh.session")

namespace
{

constexpr std::string_view remoteShellHistoryCommand =
    R"(sh -c 'kind=unknown; path=; shell_name=${SHELL##*/}; case "$shell_name" in bash) kind=bash; path=${HISTFILE:-$HOME/.bash_history} ;; zsh) kind=zsh; path=${ZDOTDIR:-$HOME}/.zsh_history ;; fish) kind=fish; path=${XDG_DATA_HOME:-$HOME/.local/share}/fish/fish_history ;; esac; printf "ZTERMY-HISTORY/1 %s\n" "$kind"; if [ -n "$path" ]; then tail -c 2097152 "$path" 2>/dev/null || true; fi')";
constexpr qsizetype maximumRemoteHistoryBytes = qsizetype{2} * 1024 * 1024 + 256;
constexpr std::string_view remoteHistoryMarker = "ZTERMY-HISTORY/1 ";

[[nodiscard]] QString failureStatus(const ztermy::ssh::SshFailureKind failure)
{
    using ztermy::ssh::SshFailureKind;

    switch (failure)
    {
        case SshFailureKind::NameResolutionFailed:
            return QCoreApplication::translate("SshTerminalSession", "SSH host name resolution failed");
        case SshFailureKind::ConnectionRefused:
            return QCoreApplication::translate("SshTerminalSession", "SSH connection was refused");
        case SshFailureKind::TimedOut:
            return QCoreApplication::translate("SshTerminalSession", "SSH operation timed out");
        case SshFailureKind::TransportError:
            return QCoreApplication::translate("SshTerminalSession", "SSH transport failed");
        case SshFailureKind::HostKeyChanged:
            return QCoreApplication::translate("SshTerminalSession", "SSH host key changed");
        case SshFailureKind::HostKeyInvalid:
            return QCoreApplication::translate("SshTerminalSession", "SSH host key could not be verified");
        case SshFailureKind::AuthenticationRejected:
            return QCoreApplication::translate("SshTerminalSession", "SSH authentication was rejected");
        case SshFailureKind::AuthenticationUnavailable:
            return QCoreApplication::translate("SshTerminalSession", "SSH authentication method is unavailable");
        case SshFailureKind::ChannelOpenFailed:
            return QCoreApplication::translate("SshTerminalSession", "SSH terminal channel could not be opened");
        case SshFailureKind::RemoteClosed:
            return QCoreApplication::translate("SshTerminalSession", "SSH remote host closed the connection");
        case SshFailureKind::Cancelled:
            return QCoreApplication::translate("SshTerminalSession", "SSH connection cancelled");
        case SshFailureKind::ProtocolError:
            return QCoreApplication::translate("SshTerminalSession", "SSH protocol error");
    }
    return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
}

void requireStateTransition(const std::expected<void, ztermy::ssh::SshStateError> &result, const char *operation)
{
    if (!result)
    {
        qFatal("Invalid SSH connection state transition while %s", operation);
    }
}

void startState(ztermy::ssh::SshConnectionState &state)
{
    requireStateTransition(state.start(), "starting");
}

void advanceState(ztermy::ssh::SshConnectionState &state, const ztermy::ssh::SshConnectionPhase phase)
{
    requireStateTransition(state.advanceTo(phase), "advancing");
}

void failState(ztermy::ssh::SshConnectionState &state, const ztermy::ssh::SshFailureKind failure)
{
    requireStateTransition(state.fail(failure), "failing");
}

void requestClose(ztermy::ssh::SshConnectionState &state)
{
    requireStateTransition(state.requestClose(), "requesting close");
}

void completeClose(ztermy::ssh::SshConnectionState &state)
{
    requireStateTransition(state.completeClose(), "completing close");
}

} // namespace

namespace ztermy::ssh
{

QString sshFailureStatus(const SshFailureKind failure)
{
    return failureStatus(failure);
}

SshTerminalSession::SshTerminalSession(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<SshConnectionPhase>();
    qRegisterMetaType<SshFailureKind>();
}

SshTerminalSession::~SshTerminalSession()
{
    stop();
}

diagnostics::LatencySummary SshTerminalSession::inputQueueLatencySummary() const noexcept
{
    return m_inputQueueLatency.summary();
}

std::error_code SshTerminalSession::start(SshConnectionRequest request, const terminal::TerminalGeometry geometry)
{
    stop();
    if (!validSshConnectionRequest(request) || !geometry.valid())
    {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (!m_commandWakeEvent.valid())
    {
        return std::make_error_code(std::errc::not_enough_memory);
    }

    auto engine = terminal::GhosttyTerminalEngine::create(geometry);
    if (!engine)
    {
        return engine.error();
    }

    m_engine = std::move(*engine);
    resetMetrics();
    if (!m_commandWakeEvent.reset())
    {
        m_engine.reset();
        return std::make_error_code(std::errc::io_error);
    }
    {
        std::scoped_lock lock(m_hostKeyMutex);
        m_hostKeyDecision.reset();
        m_awaitingHostKey = false;
    }
    postPhase(SshConnectionPhase::Resolving);
    postStatus(tr("Resolving SSH host"));

    m_worker = std::jthread([this, request = std::move(request), geometry](const std::stop_token &stopToken) mutable {
        try
        {
            run(request, geometry, stopToken);
        }
        catch (...)
        {
            finishWorker(tr("SSH worker failed unexpectedly"), SshConnectionPhase::Failed);
        }
    });
    return {};
}

void SshTerminalSession::stop() noexcept
{
    m_worker.request_stop();
    (void)m_commandWakeEvent.signal();
    m_hostKeyAvailable.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }

    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.clear();
        m_queuedInputBytes = 0;
    }
    {
        std::scoped_lock lock(m_snapshotMutex);
        m_pendingSnapshot.reset();
    }
    m_snapshotDeliveryScheduled.store(false);
    m_engine.reset();

    if (m_running.exchange(false))
    {
        emit runningChanged(false);
    }
}

void SshTerminalSession::confirmHostKey(const bool remember)
{
    {
        std::scoped_lock lock(m_hostKeyMutex);
        if (!m_awaitingHostKey || m_hostKeyDecision.has_value())
        {
            return;
        }
        m_hostKeyDecision = remember ? UnknownHostKeyDecision::AcceptAndRemember : UnknownHostKeyDecision::AcceptOnce;
    }
    m_hostKeyAvailable.notify_all();
}

void SshTerminalSession::rejectHostKey()
{
    {
        std::scoped_lock lock(m_hostKeyMutex);
        if (!m_awaitingHostKey || m_hostKeyDecision.has_value())
        {
            return;
        }
        m_hostKeyDecision = UnknownHostKeyDecision::Reject;
    }
    m_hostKeyAvailable.notify_all();
}

void SshTerminalSession::queueInput(const QByteArray &bytes)
{
    if (bytes.isEmpty() || !m_running.load())
    {
        return;
    }
    queueByteCommand(InputCommand{.bytes = bytes, .enqueuedAt = std::chrono::steady_clock::now()},
                     static_cast<std::size_t>(bytes.size()));
}

void SshTerminalSession::queuePaste(const QByteArray &bytes)
{
    if (bytes.isEmpty() || !m_running.load())
    {
        return;
    }
    queueByteCommand(PasteCommand{bytes}, static_cast<std::size_t>(bytes.size()));
}

void SshTerminalSession::queueByteCommand(Command command, const std::size_t byteCount)
{
    std::scoped_lock lock(m_commandMutex);
    if (byteCount > maximumQueuedInputBytes - std::min(m_queuedInputBytes, maximumQueuedInputBytes))
    {
        postStatus(tr("SSH input queue is full"));
        return;
    }
    m_queuedInputBytes += byteCount;
    m_commands.emplace_back(std::move(command));
    signalCommandWake();
}

void SshTerminalSession::requestResize(const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                       const quint32 cellHeightPixels)
{
    const terminal::TerminalGeometry geometry{
        .columns = columns,
        .rows = rows,
        .cellWidthPixels = cellWidthPixels,
        .cellHeightPixels = cellHeightPixels,
    };
    if (!geometry.valid() || !m_running.load())
    {
        return;
    }

    std::scoped_lock lock(m_commandMutex);
    if (!m_commands.empty() && std::holds_alternative<terminal::TerminalGeometry>(m_commands.back()))
    {
        m_commands.back() = geometry;
    }
    else
    {
        m_commands.emplace_back(geometry);
    }
    signalCommandWake();
}

void SshTerminalSession::requestScroll(const int rows)
{
    if (rows == 0 || !m_running.load())
    {
        return;
    }

    std::scoped_lock lock(m_commandMutex);
    if (!m_commands.empty())
    {
        if (auto *pending = std::get_if<ScrollCommand>(&m_commands.back()))
        {
            pending->rows += rows;
            signalCommandWake();
            return;
        }
    }
    m_commands.emplace_back(ScrollCommand{.rows = rows});
    signalCommandWake();
}

void SshTerminalSession::requestSelection(const quint16 startColumn, const quint16 startRow, const quint16 endColumn,
                                          const quint16 endRow, const bool rectangular)
{
    if (!m_running.load())
    {
        return;
    }

    SelectionCommand command{
        .selection =
            terminal::TerminalSelection{
                .start = {.column = startColumn, .row = startRow},
                .end = {.column = endColumn, .row = endRow},
                .rectangular = rectangular,
            },
    };
    std::scoped_lock lock(m_commandMutex);
    if (!m_commands.empty() && std::holds_alternative<SelectionCommand>(m_commands.back()))
    {
        m_commands.back() = command;
        signalCommandWake();
        return;
    }
    m_commands.emplace_back(command);
    signalCommandWake();
}

void SshTerminalSession::clearSelection()
{
    if (!m_running.load())
    {
        return;
    }
    std::scoped_lock lock(m_commandMutex);
    m_commands.emplace_back(SelectionCommand{});
    signalCommandWake();
}

void SshTerminalSession::copySelection()
{
    if (!m_running.load())
    {
        return;
    }
    std::scoped_lock lock(m_commandMutex);
    m_commands.emplace_back(CopyCommand{});
    signalCommandWake();
}

void SshTerminalSession::search(const QString &query, const bool backwards, const bool caseSensitive)
{
    if (!m_running.load())
    {
        return;
    }
    std::scoped_lock lock(m_commandMutex);
    m_commands.emplace_back(SearchCommand{
        .query = query.toUtf8(),
        .direction =
            backwards ? terminal::TerminalSearchDirection::backward : terminal::TerminalSearchDirection::forward,
        .caseSensitive = caseSensitive,
    });
    signalCommandWake();
}

void SshTerminalSession::clearSearch()
{
    if (!m_running.load())
    {
        return;
    }
    std::scoped_lock lock(m_commandMutex);
    m_commands.emplace_back(ClearSearchCommand{});
    signalCommandWake();
}

void SshTerminalSession::requestShellHistory(const quint64 requestId)
{
    if (requestId == 0 || !m_running.load())
    {
        return;
    }
    std::scoped_lock lock(m_commandMutex);
    m_commands.emplace_back(HistoryCommand{.requestId = requestId});
    signalCommandWake();
}

void SshTerminalSession::run(SshConnectionRequest &request, const terminal::TerminalGeometry geometry,
                             const std::stop_token &stopToken)
{
    SshConnectionState state;
    const auto finishFailure = [this, &state](const SshFailureKind failure, const QString &status = QString{}) {
        failState(state, failure);
        postFailure(failure);
        finishWorker(status.isEmpty() ? sshFailureStatus(failure) : status, state.phase());
    };

    startState(state);
    const SshConnectionCallbacks callbacks{
        .phaseChanged =
            [this, &state](const SshConnectionPhase phase) {
                advanceState(state, phase);
                postPhase(state.phase());
                switch (phase)
                {
                    case SshConnectionPhase::Connecting:
                        postStatus(tr("Connecting to SSH host"));
                        break;
                    case SshConnectionPhase::Handshaking:
                        postStatus(tr("Negotiating SSH connection"));
                        break;
                    case SshConnectionPhase::VerifyingHostKey:
                        postStatus(tr("Verifying SSH host key"));
                        break;
                    case SshConnectionPhase::AwaitingHostKeyConfirmation:
                        postStatus(tr("SSH host key confirmation required"));
                        break;
                    case SshConnectionPhase::Authenticating:
                        postStatus(tr("Authenticating SSH session"));
                        break;
                    case SshConnectionPhase::Disconnected:
                    case SshConnectionPhase::Resolving:
                    case SshConnectionPhase::OpeningChannel:
                    case SshConnectionPhase::Connected:
                    case SshConnectionPhase::Closing:
                    case SshConnectionPhase::Failed:
                        break;
                }
            },
        .confirmUnknownHostKey = [this, &stopToken](const QString &algorithm,
                                                    const QString &fingerprint) -> UnknownHostKeyDecision {
            {
                std::scoped_lock lock(m_hostKeyMutex);
                m_hostKeyDecision.reset();
                m_awaitingHostKey = true;
            }
            postHostKeyConfirmation(algorithm, fingerprint);

            std::unique_lock lock(m_hostKeyMutex);
            const bool decided = m_hostKeyAvailable.wait(lock, stopToken, [this] {
                return m_hostKeyDecision.has_value();
            });
            m_awaitingHostKey = false;
            return decided ? *m_hostKeyDecision : UnknownHostKeyDecision::Reject;
        },
        .hostKeyChanged =
            [this](const QString &algorithm, const QString &fingerprint) {
                postHostKeyChange(algorithm, fingerprint);
            },
    };

    auto connection = establishAuthenticatedSshConnection(request, callbacks, stopToken);
    if (!connection)
    {
        const QString status = connection.error().reason == SshBootstrapErrorReason::KnownHostsSaveFailed
                                   ? tr("SSH host key could not be saved")
                                   : QString{};
        finishFailure(connection.error().failure, status);
        return;
    }
    auto socket = std::move(connection->socket);
    auto session = std::move(connection->session);

    advanceState(state, SshConnectionPhase::OpeningChannel);
    postPhase(state.phase());
    postStatus(tr("Opening SSH terminal"));
    auto open = session->openTerminal(socket, geometry.columns, geometry.rows, "xterm-256color", 10s, stopToken);
    if (!open)
    {
        const SshFailureKind failure = sshFailureFromTransport(open.error(), true);
        finishFailure(failure);
        return;
    }

    advanceState(state, SshConnectionPhase::Connected);
    postPhase(state.phase());
    postStatus(tr("SSH terminal connected"));
    m_running.store(true);
    postRunning(true);
    publishSnapshot();

    std::array<char, std::size_t{64} * 1024U> readBuffer{};
    std::array<char, std::size_t{64} * 1024U> auxiliaryBuffer{};
    std::optional<quint64> activeHistoryRequest;
    std::optional<quint64> pendingHistoryRequest;
    QByteArray remoteHistoryOutput;
    QString remoteHistoryError;
    std::chrono::steady_clock::time_point remoteHistoryDeadline{};
    bool suppressHistoryResult = false;

    const auto beginHistoryRequest = [&](const quint64 requestId) {
        auto started = session->startAuxiliaryCommand(remoteShellHistoryCommand);
        if (!started)
        {
            postShellHistory(requestId, {}, {}, tr("Remote shell history could not be started."));
            return;
        }
        activeHistoryRequest = requestId;
        remoteHistoryOutput.clear();
        remoteHistoryError.clear();
        remoteHistoryDeadline = std::chrono::steady_clock::now() + 8s;
        suppressHistoryResult = false;
    };

    while (!stopToken.stop_requested())
    {
        std::deque<Command> commands;
        {
            std::scoped_lock lock(m_commandMutex);
            commands.swap(m_commands);
            m_queuedInputBytes = 0;
            if (!m_commandWakeEvent.reset())
            {
                finishFailure(SshFailureKind::ProtocolError, tr("SSH command wake event failed"));
                return;
            }
        }

        for (const Command &command : commands)
        {
            if (const auto *input = std::get_if<InputCommand>(&command))
            {
                m_inputQueueLatency.record(std::chrono::steady_clock::now() - input->enqueuedAt);
                const std::error_code selectionError = m_engine->setSelection(std::nullopt);
                m_engine->scrollToBottom();
                if (selectionError)
                {
                    postStatus(tr("SSH terminal selection clear failed: %1")
                                   .arg(QString::fromStdString(selectionError.message())));
                }
                publishSnapshot();

                const auto bytes = std::span(input->bytes.constData(), static_cast<std::size_t>(input->bytes.size()));
                auto written = session->writeTerminal(socket, bytes, 10s, stopToken);
                if (!written)
                {
                    const SshFailureKind failure = sshFailureFromTransport(written.error());
                    finishFailure(failure);
                    return;
                }
                continue;
            }

            if (const auto *paste = std::get_if<PasteCommand>(&command))
            {
                const std::error_code selectionError = m_engine->setSelection(std::nullopt);
                m_engine->scrollToBottom();
                if (selectionError)
                {
                    postStatus(tr("SSH terminal selection clear failed: %1")
                                   .arg(QString::fromStdString(selectionError.message())));
                }
                const auto bytes =
                    std::as_bytes(std::span(paste->bytes.constData(), static_cast<std::size_t>(paste->bytes.size())));
                auto encoded = m_engine->encodePaste(bytes);
                if (!encoded)
                {
                    postStatus(
                        tr("SSH terminal paste failed: %1").arg(QString::fromStdString(encoded.error().message())));
                    continue;
                }
                publishSnapshot();

                const auto encodedBytes = std::span(reinterpret_cast<const char *>(encoded->data()), encoded->size());
                auto written = session->writeTerminal(socket, encodedBytes, 10s, stopToken);
                if (!written)
                {
                    const SshFailureKind failure = sshFailureFromTransport(written.error());
                    finishFailure(failure);
                    return;
                }
                continue;
            }

            if (const auto *scroll = std::get_if<ScrollCommand>(&command))
            {
                m_engine->scrollViewport(scroll->rows);
                publishSnapshot();
                continue;
            }

            if (const auto *selection = std::get_if<SelectionCommand>(&command))
            {
                if (const std::error_code error = m_engine->setSelection(selection->selection))
                {
                    postStatus(tr("SSH terminal selection failed: %1").arg(QString::fromStdString(error.message())));
                    continue;
                }
                publishSnapshot();
                continue;
            }

            if (std::holds_alternative<CopyCommand>(command))
            {
                auto selectedText = m_engine->selectedText();
                if (!selectedText)
                {
                    postStatus(
                        tr("SSH terminal copy failed: %1").arg(QString::fromStdString(selectedText.error().message())));
                }
                else if (*selectedText)
                {
                    const QString text =
                        QString::fromUtf8((*selectedText)->data(), static_cast<qsizetype>((*selectedText)->size()));
                    postClipboardText(text);
                }
                continue;
            }

            if (const auto *search = std::get_if<SearchCommand>(&command))
            {
                auto result = m_engine->search(
                    std::string_view(search->query.constData(), static_cast<std::size_t>(search->query.size())),
                    search->direction, search->caseSensitive);
                if (!result)
                {
                    postStatus(
                        tr("SSH terminal search failed: %1").arg(QString::fromStdString(result.error().message())));
                    continue;
                }
                const QString query = QString::fromUtf8(search->query);
                const terminal::TerminalSearchResult searchResult = *result;
                postSearchResult(query, searchResult.current, searchResult.total, searchResult.wrapped);
                publishSnapshot();
                continue;
            }

            if (std::holds_alternative<ClearSearchCommand>(command))
            {
                if (const std::error_code error = m_engine->clearSearch())
                {
                    postStatus(tr("SSH terminal search clear failed: %1").arg(QString::fromStdString(error.message())));
                    continue;
                }
                postSearchResult({}, 0, 0, false);
                publishSnapshot();
                continue;
            }

            if (const auto *history = std::get_if<HistoryCommand>(&command))
            {
                pendingHistoryRequest = history->requestId;
                if (activeHistoryRequest)
                {
                    suppressHistoryResult = true;
                    session->cancelAuxiliaryCommand();
                }
                continue;
            }

            const auto requested = std::get<terminal::TerminalGeometry>(command);
            auto resized = session->resizeTerminal(socket, requested.columns, requested.rows, 5s, stopToken);
            if (!resized)
            {
                const SshFailureKind failure = sshFailureFromTransport(resized.error());
                finishFailure(failure);
                return;
            }
            if (const std::error_code error = m_engine->resize(requested))
            {
                finishFailure(SshFailureKind::ProtocolError, tr("SSH terminal state resize failed"));
                return;
            }
            publishSnapshot();
        }

        if (!activeHistoryRequest && pendingHistoryRequest && !session->auxiliaryCommandActive())
        {
            const quint64 requestId = *pendingHistoryRequest;
            pendingHistoryRequest.reset();
            beginHistoryRequest(requestId);
        }

        if (activeHistoryRequest)
        {
            if (remoteHistoryError.isEmpty() && std::chrono::steady_clock::now() >= remoteHistoryDeadline)
            {
                remoteHistoryError = tr("Remote shell history timed out.");
                session->cancelAuxiliaryCommand();
            }

            auto polled = session->pollAuxiliaryCommand(auxiliaryBuffer);
            if (!polled)
            {
                if (remoteHistoryError.isEmpty())
                {
                    remoteHistoryError = tr("Remote shell history could not be read.");
                }
                session->cancelAuxiliaryCommand();
            }
            else if (polled->progress == AuxiliaryCommandProgress::Output)
            {
                const auto bytesRead = static_cast<qsizetype>(polled->bytesRead);
                if (bytesRead > maximumRemoteHistoryBytes - remoteHistoryOutput.size())
                {
                    remoteHistoryError = tr("Remote shell history exceeded the safety limit.");
                    session->cancelAuxiliaryCommand();
                }
                else
                {
                    remoteHistoryOutput.append(auxiliaryBuffer.data(), bytesRead);
                }
            }
            else if (polled->progress == AuxiliaryCommandProgress::Completed)
            {
                const quint64 requestId = *activeHistoryRequest;
                QString shell;
                QByteArray contents;
                if (remoteHistoryError.isEmpty() && polled->exitStatus != 0)
                {
                    remoteHistoryError = tr("Remote shell history command failed.");
                }
                if (remoteHistoryError.isEmpty())
                {
                    const qsizetype newline = remoteHistoryOutput.indexOf('\n');
                    const QByteArray marker(remoteHistoryMarker.data(),
                                            static_cast<qsizetype>(remoteHistoryMarker.size()));
                    if (newline < marker.size() || !remoteHistoryOutput.startsWith(marker))
                    {
                        remoteHistoryError = tr("Remote shell history returned an invalid response.");
                    }
                    else
                    {
                        shell = QString::fromLatin1(remoteHistoryOutput.sliced(marker.size(), newline - marker.size()));
                        contents = remoteHistoryOutput.sliced(newline + 1);
                        if (shell != QStringLiteral("bash") && shell != QStringLiteral("zsh")
                            && shell != QStringLiteral("fish"))
                        {
                            shell.clear();
                            contents.clear();
                            remoteHistoryError = tr("This remote shell is not supported yet.");
                        }
                    }
                }
                if (!suppressHistoryResult)
                {
                    postShellHistory(requestId, shell, contents, remoteHistoryError);
                }
                activeHistoryRequest.reset();
                remoteHistoryOutput.clear();
                remoteHistoryError.clear();
                suppressHistoryResult = false;
            }
        }

        auto read = session->readTerminal(socket, readBuffer, 25ms, stopToken, m_commandWakeEvent.nativeHandle());
        if (!read)
        {
            if (read.error().kind == SshTransportErrorKind::TimedOut)
            {
                continue;
            }
            if (read.error().kind == SshTransportErrorKind::Cancelled && stopToken.stop_requested())
            {
                break;
            }
            if (read.error().kind == SshTransportErrorKind::Cancelled)
            {
                continue;
            }
            const SshFailureKind failure = sshFailureFromTransport(read.error());
            finishFailure(failure);
            return;
        }
        if (*read == 0)
        {
            finishFailure(SshFailureKind::RemoteClosed);
            return;
        }

        const auto bytes = std::as_bytes(std::span(readBuffer).first(*read));
        if (const std::error_code error = m_engine->feed(bytes))
        {
            finishFailure(SshFailureKind::ProtocolError, tr("SSH terminal parser failed"));
            return;
        }
        publishSnapshot();
    }

    requestClose(state);
    postPhase(state.phase());
    if (session->terminalOpen())
    {
        const auto close = session->closeTerminal(socket, 2s);
        if (!close)
        {
            postStatus(tr("SSH terminal closed without a complete channel shutdown"));
        }
    }
    completeClose(state);
    finishWorker(tr("SSH terminal disconnected"), state.phase());
}

void SshTerminalSession::publishSnapshot()
{
    auto result = m_engine->snapshot();
    if (!result)
    {
        postStatus(tr("SSH terminal snapshot failed"));
        return;
    }

    {
        std::scoped_lock lock(m_snapshotMutex);
        m_pendingSnapshot = std::make_shared<const terminal::TerminalSnapshot>(std::move(*result));
    }
    if (!m_snapshotDeliveryScheduled.exchange(true))
    {
        (void)QMetaObject::invokeMethod(this, "deliverLatestSnapshot", Qt::QueuedConnection);
    }
}

void SshTerminalSession::deliverLatestSnapshot()
{
    terminal::TerminalSnapshotPtr snapshot;
    {
        std::scoped_lock lock(m_snapshotMutex);
        snapshot = std::move(m_pendingSnapshot);
    }
    m_snapshotDeliveryScheduled.store(false);
    if (snapshot)
    {
        emit snapshotReady(std::move(snapshot));
    }

    {
        std::scoped_lock lock(m_snapshotMutex);
        if (!m_pendingSnapshot || m_snapshotDeliveryScheduled.exchange(true))
        {
            return;
        }
    }
    (void)QMetaObject::invokeMethod(this, "deliverLatestSnapshot", Qt::QueuedConnection);
}

void SshTerminalSession::postStatus(const QString &status)
{
    if (QThread::currentThread() == thread())
    {
        deliverStatus(status);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverStatus", Qt::QueuedConnection, Q_ARG(QString, status)))
    {
        qCWarning(sshSessionLog) << "SSH status could not be queued to its owner thread";
    }
}

void SshTerminalSession::postPhase(const SshConnectionPhase phase)
{
    if (QThread::currentThread() == thread())
    {
        deliverPhase(phase);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverPhase", Qt::QueuedConnection,
                                   Q_ARG(ztermy::ssh::SshConnectionPhase, phase)))
    {
        qCWarning(sshSessionLog) << "SSH phase could not be queued to its owner thread";
    }
}

void SshTerminalSession::postFailure(const SshFailureKind failure)
{
    if (QThread::currentThread() == thread())
    {
        deliverFailure(failure);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverFailure", Qt::QueuedConnection,
                                   Q_ARG(ztermy::ssh::SshFailureKind, failure)))
    {
        qCWarning(sshSessionLog) << "SSH failure could not be queued to its owner thread";
    }
}

void SshTerminalSession::postRunning(const bool running)
{
    if (QThread::currentThread() == thread())
    {
        deliverRunning(running);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverRunning", Qt::QueuedConnection, Q_ARG(bool, running)))
    {
        qCWarning(sshSessionLog) << "SSH running state could not be queued to its owner thread";
    }
}

void SshTerminalSession::postHostKeyConfirmation(const QString &algorithm, const QString &fingerprint)
{
    if (QThread::currentThread() == thread())
    {
        deliverHostKeyConfirmation(algorithm, fingerprint);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverHostKeyConfirmation", Qt::QueuedConnection, Q_ARG(QString, algorithm),
                                   Q_ARG(QString, fingerprint)))
    {
        qCWarning(sshSessionLog) << "SSH host-key confirmation could not be queued to its owner thread";
    }
}

void SshTerminalSession::postHostKeyChange(const QString &algorithm, const QString &fingerprint)
{
    if (QThread::currentThread() == thread())
    {
        deliverHostKeyChange(algorithm, fingerprint);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverHostKeyChange", Qt::QueuedConnection, Q_ARG(QString, algorithm),
                                   Q_ARG(QString, fingerprint)))
    {
        qCWarning(sshSessionLog) << "SSH host-key change could not be queued to its owner thread";
    }
}

void SshTerminalSession::postClipboardText(const QString &text)
{
    if (QThread::currentThread() == thread())
    {
        deliverClipboardText(text);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverClipboardText", Qt::QueuedConnection, Q_ARG(QString, text)))
    {
        qCWarning(sshSessionLog) << "SSH clipboard text could not be queued to its owner thread";
    }
}

void SshTerminalSession::postSearchResult(const QString &query, const quint32 current, const quint32 total,
                                          const bool wrapped)
{
    if (QThread::currentThread() == thread())
    {
        deliverSearchResult(query, current, total, wrapped);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverSearchResult", Qt::QueuedConnection, Q_ARG(QString, query),
                                   Q_ARG(quint32, current), Q_ARG(quint32, total), Q_ARG(bool, wrapped)))
    {
        qCWarning(sshSessionLog) << "SSH search result could not be queued to its owner thread";
    }
}

void SshTerminalSession::postShellHistory(const quint64 requestId, const QString &shell, const QByteArray &contents,
                                          const QString &error)
{
    if (QThread::currentThread() == thread())
    {
        deliverShellHistory(requestId, shell, contents, error);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverShellHistory", Qt::QueuedConnection, Q_ARG(quint64, requestId),
                                   Q_ARG(QString, shell), Q_ARG(QByteArray, contents), Q_ARG(QString, error)))
    {
        qCWarning(sshSessionLog) << "SSH shell-history result could not be queued to its owner thread";
    }
}

void SshTerminalSession::finishWorker(const QString &status, const SshConnectionPhase phase)
{
    logMetrics();
    if (m_running.exchange(false))
    {
        postRunning(false);
    }
    postPhase(phase);
    postStatus(status);
}

void SshTerminalSession::deliverStatus(const QString &status)
{
    emit statusChanged(status);
}

void SshTerminalSession::deliverPhase(const SshConnectionPhase phase)
{
    emit phaseChanged(phase);
}

void SshTerminalSession::deliverFailure(const SshFailureKind failure)
{
    emit failureOccurred(failure);
}

void SshTerminalSession::deliverRunning(const bool running)
{
    emit runningChanged(running);
}

void SshTerminalSession::deliverHostKeyConfirmation(const QString &algorithm, const QString &fingerprint)
{
    emit hostKeyConfirmationRequired(algorithm, fingerprint);
}

void SshTerminalSession::deliverHostKeyChange(const QString &algorithm, const QString &fingerprint)
{
    emit hostKeyChanged(algorithm, fingerprint);
}

void SshTerminalSession::deliverClipboardText(const QString &text)
{
    emit clipboardTextReady(text);
}

void SshTerminalSession::deliverSearchResult(const QString &query, const quint32 current, const quint32 total,
                                             const bool wrapped)
{
    emit searchResultReady(query, current, total, wrapped);
}

void SshTerminalSession::deliverShellHistory(const quint64 requestId, const QString &shell, const QByteArray &contents,
                                             const QString &error)
{
    emit shellHistoryReady(requestId, shell, contents, error);
}

void SshTerminalSession::signalCommandWake() noexcept
{
    if (!m_commandWakeEvent.signal())
    {
        qCWarning(sshSessionLog) << "SSH command wake event could not be signaled";
    }
}

void SshTerminalSession::resetMetrics() noexcept
{
    m_inputQueueLatency.reset();
}

void SshTerminalSession::logMetrics() const
{
    const diagnostics::LatencySummary inputQueueLatency = m_inputQueueLatency.summary();
    qCInfo(sshSessionLog) << "SSH session metrics"
                          << "inputQueueSamples=" << inputQueueLatency.count
                          << "inputQueueP50Us=" << inputQueueLatency.p50UpperBoundMicroseconds
                          << "inputQueueP95Us=" << inputQueueLatency.p95UpperBoundMicroseconds
                          << "inputQueueP99Us=" << inputQueueLatency.p99UpperBoundMicroseconds
                          << "inputQueueMaxUs=" << inputQueueLatency.maxMicroseconds;
}

} // namespace ztermy::ssh
