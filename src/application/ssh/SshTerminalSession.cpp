#include "application/ssh/SshTerminalSession.h"

#include "domain/ssh/SshHostKey.h"
#include "domain/terminal/GhosttyTerminalEngine.h"
#include "infrastructure/ssh/KnownHostsStore.h"
#include "infrastructure/ssh/Libssh2Session.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <QLoggingCategory>
#include <QMetaObject>

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

[[nodiscard]] bool validRequest(const ztermy::ssh::SshConnectionRequest &request) noexcept
{
    if (request.host.trimmed().isEmpty() || request.port == 0 || request.username.isEmpty()
        || request.knownHostsPath.isEmpty())
    {
        return false;
    }
    switch (request.authentication)
    {
        case ztermy::ssh::SshAuthenticationMethod::PrivateKey:
            return !request.privateKeyPath.isEmpty();
        case ztermy::ssh::SshAuthenticationMethod::Password:
            return request.privateKeyPath.isEmpty() && !request.secret.empty();
    }
    return false;
}

[[nodiscard]] ztermy::ssh::SshFailureKind mapTcpFailure(const ztermy::ssh::TcpConnectErrorKind kind) noexcept
{
    using ztermy::ssh::SshFailureKind;
    using ztermy::ssh::TcpConnectErrorKind;

    switch (kind)
    {
        case TcpConnectErrorKind::NameResolutionFailed:
            return SshFailureKind::NameResolutionFailed;
        case TcpConnectErrorKind::ConnectionRefused:
            return SshFailureKind::ConnectionRefused;
        case TcpConnectErrorKind::TimedOut:
            return SshFailureKind::TimedOut;
        case TcpConnectErrorKind::Cancelled:
            return SshFailureKind::Cancelled;
        case TcpConnectErrorKind::InvalidEndpoint:
        case TcpConnectErrorKind::NetworkUnreachable:
        case TcpConnectErrorKind::SystemError:
            return SshFailureKind::TransportError;
    }
    return SshFailureKind::TransportError;
}

[[nodiscard]] ztermy::ssh::SshFailureKind mapTransportFailure(const ztermy::ssh::SshTransportError &error,
                                                              const bool openingChannel = false) noexcept
{
    using ztermy::ssh::SshFailureKind;
    using ztermy::ssh::SshTransportErrorKind;

    switch (error.kind)
    {
        case SshTransportErrorKind::TimedOut:
            return SshFailureKind::TimedOut;
        case SshTransportErrorKind::Cancelled:
            return SshFailureKind::Cancelled;
        case SshTransportErrorKind::AuthenticationRejected:
            return SshFailureKind::AuthenticationRejected;
        case SshTransportErrorKind::AuthenticationUnavailable:
            return SshFailureKind::AuthenticationUnavailable;
        case SshTransportErrorKind::ConnectionLost:
            return SshFailureKind::RemoteClosed;
        case SshTransportErrorKind::InitializationFailed:
        case SshTransportErrorKind::InvalidArgument:
        case SshTransportErrorKind::InvalidState:
        case SshTransportErrorKind::ProtocolError:
            return openingChannel ? SshFailureKind::ChannelOpenFailed : SshFailureKind::ProtocolError;
    }
    return SshFailureKind::ProtocolError;
}

[[nodiscard]] QString failureStatus(const ztermy::ssh::SshFailureKind failure)
{
    using ztermy::ssh::SshFailureKind;

    switch (failure)
    {
        case SshFailureKind::NameResolutionFailed:
            return QStringLiteral("SSH host name resolution failed");
        case SshFailureKind::ConnectionRefused:
            return QStringLiteral("SSH connection was refused");
        case SshFailureKind::TimedOut:
            return QStringLiteral("SSH operation timed out");
        case SshFailureKind::TransportError:
            return QStringLiteral("SSH transport failed");
        case SshFailureKind::HostKeyChanged:
            return QStringLiteral("SSH host key changed");
        case SshFailureKind::HostKeyInvalid:
            return QStringLiteral("SSH host key could not be verified");
        case SshFailureKind::AuthenticationRejected:
            return QStringLiteral("SSH authentication was rejected");
        case SshFailureKind::AuthenticationUnavailable:
            return QStringLiteral("SSH authentication method is unavailable");
        case SshFailureKind::ChannelOpenFailed:
            return QStringLiteral("SSH terminal channel could not be opened");
        case SshFailureKind::RemoteClosed:
            return QStringLiteral("SSH remote host closed the connection");
        case SshFailureKind::Cancelled:
            return QStringLiteral("SSH connection cancelled");
        case SshFailureKind::ProtocolError:
            return QStringLiteral("SSH protocol error");
    }
    return QStringLiteral("SSH connection failed");
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
    if (!validRequest(request) || !geometry.valid())
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
        m_hostKeyDecision = HostKeyDecision::Pending;
        m_awaitingHostKey = false;
    }
    postPhase(SshConnectionPhase::Resolving);
    postStatus(QStringLiteral("Resolving SSH host"));

    m_worker = std::jthread([this, request = std::move(request), geometry](const std::stop_token &stopToken) mutable {
        try
        {
            run(request, geometry, stopToken);
        }
        catch (...)
        {
            finishWorker(QStringLiteral("SSH worker failed unexpectedly"), SshConnectionPhase::Failed);
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
        if (!m_awaitingHostKey || m_hostKeyDecision != HostKeyDecision::Pending)
        {
            return;
        }
        m_hostKeyDecision = remember ? HostKeyDecision::AcceptAndRemember : HostKeyDecision::AcceptOnce;
    }
    m_hostKeyAvailable.notify_all();
}

void SshTerminalSession::rejectHostKey()
{
    {
        std::scoped_lock lock(m_hostKeyMutex);
        if (!m_awaitingHostKey || m_hostKeyDecision != HostKeyDecision::Pending)
        {
            return;
        }
        m_hostKeyDecision = HostKeyDecision::Reject;
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
        postStatus(QStringLiteral("SSH input queue is full"));
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
    m_commands.emplace_back(geometry);
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

void SshTerminalSession::run(SshConnectionRequest &request, const terminal::TerminalGeometry geometry,
                             const std::stop_token &stopToken)
{
    SshConnectionState state;
    const auto finishFailure = [this, &state](const SshFailureKind failure, const QString &status = QString{}) {
        failState(state, failure);
        emit failureOccurred(failure);
        finishWorker(status.isEmpty() ? sshFailureStatus(failure) : status, state.phase());
    };

    startState(state);
    advanceState(state, SshConnectionPhase::Connecting);
    postPhase(state.phase());
    postStatus(QStringLiteral("Connecting to SSH host"));

    const QByteArray hostUtf8 = request.host.trimmed().toUtf8();
    const std::string host(hostUtf8.constData(), static_cast<std::size_t>(hostUtf8.size()));
    auto socket = WindowsTcpSocket::connect(host, request.port, 10s, stopToken);
    if (!socket)
    {
        const SshFailureKind failure = mapTcpFailure(socket.error().kind);
        finishFailure(failure);
        return;
    }

    auto session = Libssh2Session::create();
    if (!session)
    {
        finishFailure(SshFailureKind::ProtocolError);
        return;
    }

    advanceState(state, SshConnectionPhase::Handshaking);
    postPhase(state.phase());
    postStatus(QStringLiteral("Negotiating SSH connection"));
    auto handshake = (*session)->handshake(*socket, 10s, stopToken);
    if (!handshake)
    {
        const SshFailureKind failure = mapTransportFailure(handshake.error());
        finishFailure(failure);
        return;
    }

    advanceState(state, SshConnectionPhase::VerifyingHostKey);
    postPhase(state.phase());
    postStatus(QStringLiteral("Verifying SSH host key"));
    auto hostKey = (*session)->hostKey();
    const KnownHostsStore knownHostsStore(request.knownHostsPath);
    auto knownHosts = knownHostsStore.load();
    if (!hostKey || !knownHosts)
    {
        finishFailure(SshFailureKind::HostKeyInvalid);
        return;
    }

    const SshEndpoint endpoint{.host = host, .port = request.port};
    auto trust = (*session)->verifyHostKey(endpoint, *knownHosts);
    if (!trust)
    {
        finishFailure(SshFailureKind::HostKeyInvalid);
        return;
    }

    const QString algorithm = QString::fromUtf8(hostKeyAlgorithmName(hostKey->algorithm));
    const QString fingerprint = QString::fromStdString(sha256Fingerprint(*hostKey));
    if (*trust == HostKeyTrust::Changed)
    {
        emit hostKeyChanged(algorithm, fingerprint);
        finishFailure(SshFailureKind::HostKeyChanged);
        return;
    }

    if (*trust == HostKeyTrust::Unknown)
    {
        advanceState(state, SshConnectionPhase::AwaitingHostKeyConfirmation);
        postPhase(state.phase());
        postStatus(QStringLiteral("SSH host key confirmation required"));
        {
            std::scoped_lock lock(m_hostKeyMutex);
            m_awaitingHostKey = true;
        }
        emit hostKeyConfirmationRequired(algorithm, fingerprint);

        HostKeyDecision decision = HostKeyDecision::Pending;
        {
            std::unique_lock lock(m_hostKeyMutex);
            if (!m_hostKeyAvailable.wait(lock, stopToken, [this] {
                    return m_hostKeyDecision != HostKeyDecision::Pending;
                }))
            {
                decision = HostKeyDecision::Reject;
            }
            else
            {
                decision = m_hostKeyDecision;
            }
            m_awaitingHostKey = false;
        }
        if (decision == HostKeyDecision::Reject)
        {
            const SshFailureKind failure =
                stopToken.stop_requested() ? SshFailureKind::Cancelled : SshFailureKind::HostKeyInvalid;
            finishFailure(failure);
            return;
        }

        knownHosts->push_back(KnownHostEntry{
            .endpoint = endpoint,
            .algorithm = hostKey->algorithm,
            .encodedKey = hostKey->encodedKey,
        });
        if (decision == HostKeyDecision::AcceptAndRemember && !knownHostsStore.save(*knownHosts))
        {
            finishFailure(SshFailureKind::HostKeyInvalid, QStringLiteral("SSH host key could not be saved"));
            return;
        }
        trust = (*session)->verifyHostKey(endpoint, *knownHosts);
        if (!trust || *trust != HostKeyTrust::Trusted)
        {
            finishFailure(SshFailureKind::HostKeyInvalid);
            return;
        }
    }

    advanceState(state, SshConnectionPhase::Authenticating);
    postPhase(state.phase());
    postStatus(QStringLiteral("Authenticating SSH session"));
    const QByteArray usernameUtf8 = request.username.toUtf8();
    const QByteArray privateKeyPathUtf8 = request.privateKeyPath.toUtf8();
    const std::string username(usernameUtf8.constData(), static_cast<std::size_t>(usernameUtf8.size()));
    const std::string privateKeyPath(privateKeyPathUtf8.constData(),
                                     static_cast<std::size_t>(privateKeyPathUtf8.size()));
    std::expected<void, SshTransportError> authentication;
    switch (request.authentication)
    {
        case SshAuthenticationMethod::PrivateKey:
            authentication = (*session)->authenticateWithPrivateKeyFile(*socket, username, privateKeyPath,
                                                                        request.secret.view(), 15s, stopToken);
            break;
        case SshAuthenticationMethod::Password:
            authentication =
                (*session)->authenticateWithPassword(*socket, username, request.secret.view(), 15s, stopToken);
            break;
    }
    request.secret.clear();
    if (!authentication)
    {
        const SshFailureKind failure = mapTransportFailure(authentication.error());
        finishFailure(failure);
        return;
    }

    advanceState(state, SshConnectionPhase::OpeningChannel);
    postPhase(state.phase());
    postStatus(QStringLiteral("Opening SSH terminal"));
    auto open = (*session)->openTerminal(*socket, geometry.columns, geometry.rows, "xterm-256color", 10s, stopToken);
    if (!open)
    {
        const SshFailureKind failure = mapTransportFailure(open.error(), true);
        finishFailure(failure);
        return;
    }

    advanceState(state, SshConnectionPhase::Connected);
    postPhase(state.phase());
    postStatus(QStringLiteral("SSH terminal connected"));
    m_running.store(true);
    emit runningChanged(true);
    publishSnapshot();

    std::array<char, std::size_t{64} * 1024U> readBuffer{};
    while (!stopToken.stop_requested())
    {
        std::deque<Command> commands;
        {
            std::scoped_lock lock(m_commandMutex);
            commands.swap(m_commands);
            m_queuedInputBytes = 0;
            if (!m_commandWakeEvent.reset())
            {
                finishFailure(SshFailureKind::ProtocolError, QStringLiteral("SSH command wake event failed"));
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
                    postStatus(QStringLiteral("SSH terminal selection clear failed: %1")
                                   .arg(QString::fromStdString(selectionError.message())));
                }
                publishSnapshot();

                const auto bytes = std::span(input->bytes.constData(), static_cast<std::size_t>(input->bytes.size()));
                auto written = (*session)->writeTerminal(*socket, bytes, 10s, stopToken);
                if (!written)
                {
                    const SshFailureKind failure = mapTransportFailure(written.error());
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
                    postStatus(QStringLiteral("SSH terminal selection clear failed: %1")
                                   .arg(QString::fromStdString(selectionError.message())));
                }
                const auto bytes =
                    std::as_bytes(std::span(paste->bytes.constData(), static_cast<std::size_t>(paste->bytes.size())));
                auto encoded = m_engine->encodePaste(bytes);
                if (!encoded)
                {
                    postStatus(QStringLiteral("SSH terminal paste failed: %1")
                                   .arg(QString::fromStdString(encoded.error().message())));
                    continue;
                }
                publishSnapshot();

                const auto encodedBytes = std::span(reinterpret_cast<const char *>(encoded->data()), encoded->size());
                auto written = (*session)->writeTerminal(*socket, encodedBytes, 10s, stopToken);
                if (!written)
                {
                    const SshFailureKind failure = mapTransportFailure(written.error());
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
                    postStatus(QStringLiteral("SSH terminal selection failed: %1")
                                   .arg(QString::fromStdString(error.message())));
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
                    postStatus(QStringLiteral("SSH terminal copy failed: %1")
                                   .arg(QString::fromStdString(selectedText.error().message())));
                }
                else if (*selectedText)
                {
                    emit clipboardTextReady(
                        QString::fromUtf8((*selectedText)->data(), static_cast<qsizetype>((*selectedText)->size())));
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
                    postStatus(QStringLiteral("SSH terminal search failed: %1")
                                   .arg(QString::fromStdString(result.error().message())));
                    continue;
                }
                emit searchResultReady(QString::fromUtf8(search->query), result->current, result->total,
                                       result->wrapped);
                publishSnapshot();
                continue;
            }

            if (std::holds_alternative<ClearSearchCommand>(command))
            {
                if (const std::error_code error = m_engine->clearSearch())
                {
                    postStatus(QStringLiteral("SSH terminal search clear failed: %1")
                                   .arg(QString::fromStdString(error.message())));
                    continue;
                }
                emit searchResultReady({}, 0, 0, false);
                publishSnapshot();
                continue;
            }

            const auto requested = std::get<terminal::TerminalGeometry>(command);
            auto resized = (*session)->resizeTerminal(*socket, requested.columns, requested.rows, 5s, stopToken);
            if (!resized)
            {
                const SshFailureKind failure = mapTransportFailure(resized.error());
                finishFailure(failure);
                return;
            }
            if (const std::error_code error = m_engine->resize(requested))
            {
                finishFailure(SshFailureKind::ProtocolError, QStringLiteral("SSH terminal state resize failed"));
                return;
            }
            publishSnapshot();
        }

        auto read = (*session)->readTerminal(*socket, readBuffer, 25ms, stopToken, m_commandWakeEvent.nativeHandle());
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
            const SshFailureKind failure = mapTransportFailure(read.error());
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
            finishFailure(SshFailureKind::ProtocolError, QStringLiteral("SSH terminal parser failed"));
            return;
        }
        publishSnapshot();
    }

    requestClose(state);
    postPhase(state.phase());
    if ((*session)->terminalOpen())
    {
        const auto close = (*session)->closeTerminal(*socket, 2s);
        if (!close)
        {
            postStatus(QStringLiteral("SSH terminal closed without a complete channel shutdown"));
        }
    }
    completeClose(state);
    finishWorker(QStringLiteral("SSH terminal disconnected"), state.phase());
}

void SshTerminalSession::publishSnapshot()
{
    auto result = m_engine->snapshot();
    if (!result)
    {
        postStatus(QStringLiteral("SSH terminal snapshot failed"));
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
    emit statusChanged(status);
}

void SshTerminalSession::postPhase(const SshConnectionPhase phase)
{
    emit phaseChanged(phase);
}

void SshTerminalSession::finishWorker(const QString &status, const SshConnectionPhase phase)
{
    logMetrics();
    if (m_running.exchange(false))
    {
        emit runningChanged(false);
    }
    postPhase(phase);
    postStatus(status);
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
