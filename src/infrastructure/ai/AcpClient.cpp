#include "infrastructure/ai/AcpClient.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>

#include <array>
#include <cmath>
#include <ranges>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maximumStderrBytes = qsizetype{64} * 1024;
constexpr qsizetype maximumPendingAgentRequests = 32;
constexpr qsizetype maximumAgentRequestsPerPrompt = 256;

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString requestKey(const QJsonValue &id)
{
    if (id.isString())
    {
        return QStringLiteral("s:") + id.toString();
    }
    if (id.isDouble())
    {
        return QStringLiteral("n:") + QString::number(id.toDouble(), 'g', 17);
    }
    return {};
}

[[nodiscard]] bool matchesRequestId(const QJsonValue &id, const std::uint64_t expected) noexcept
{
    if (!id.isDouble())
    {
        return false;
    }
    const double value = id.toDouble();
    return std::isfinite(value) && value == static_cast<double>(expected);
}

[[nodiscard]] QString responseError(const AcpMessage &message, const QString &fallback)
{
    const QString detail = message.error.value(QStringLiteral("message")).toString();
    return detail.isEmpty() ? fallback : QStringLiteral("%1: %2").arg(fallback, detail);
}

[[nodiscard]] bool supportedAgentRequest(const QString &method)
{
    static constexpr std::array<std::string_view, 6> supported{
        "session/request_permission", "terminal/create", "terminal/output",
        "terminal/wait_for_exit",     "terminal/kill",   "terminal/release",
    };
    return std::ranges::any_of(supported, [&method](const std::string_view candidate) {
        return method == QString::fromLatin1(candidate.data(), static_cast<qsizetype>(candidate.size()));
    });
}

[[nodiscard]] bool validStopReason(const QString &reason)
{
    static const std::array reasons{QStringLiteral("end_turn"), QStringLiteral("max_tokens"),
                                    QStringLiteral("max_turn_requests"), QStringLiteral("refusal"),
                                    QStringLiteral("cancelled")};
    return std::ranges::find(reasons, reason) != reasons.end();
}

} // namespace

AcpClient::AcpClient(QObject *parent) : QObject(parent)
{
    QObject::connect(&m_process, &QProcess::started, this, [this] {
        handleStarted();
    });
    QObject::connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        handleReadyRead();
    });
    QObject::connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        QByteArray discarded = m_process.readAllStandardError();
        if (discarded.size() > maximumStderrBytes)
        {
            discarded.truncate(maximumStderrBytes);
        }
    });
    QObject::connect(&m_process, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError) {
        fail(QStringLiteral("The ACP Agent process failed: %1").arg(m_process.errorString()));
    });
    QObject::connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                     [this](const int exitCode, const QProcess::ExitStatus) {
                         if (m_state != State::stopped && m_state != State::failed)
                         {
                             fail(QStringLiteral("The ACP Agent exited unexpectedly (code %1).").arg(exitCode));
                         }
                     });

    m_handshakeDeadline.setSingleShot(true);
    QObject::connect(&m_handshakeDeadline, &QTimer::timeout, this, [this] {
        if (m_state == State::starting || m_state == State::initializing || m_state == State::openingSession)
        {
            fail(QStringLiteral("The ACP Agent did not complete its handshake in time."));
        }
    });
    m_cancelDeadline.setSingleShot(true);
    QObject::connect(&m_cancelDeadline, &QTimer::timeout, this, [this] {
        if (m_state == State::promptActive && m_cancellationRequested)
        {
            fail(QStringLiteral("The ACP Agent did not finish cancellation in time."));
        }
    });
}

AcpClient::~AcpClient()
{
    stop();
}

std::expected<void, QString> AcpClient::start(AcpClientConfiguration configuration, ReadyHandler readyHandler,
                                              UpdateHandler updateHandler, RequestHandler requestHandler)
{
    stop();
    const QFileInfo executable(configuration.program);
    if (!executable.isAbsolute() || !executable.exists() || !executable.isFile())
    {
        return std::unexpected(QStringLiteral("ACP requires an existing absolute Agent executable path."));
    }
    if (!QDir::isAbsolutePath(configuration.workingDirectory) || !QFileInfo(configuration.workingDirectory).isDir()
        || configuration.clientVersion.empty())
    {
        return std::unexpected(QStringLiteral("ACP requires a valid absolute working directory and client version."));
    }

    auto initializeRequest = m_protocol.initializeRequest(initializeRequestId, configuration.clientVersion,
                                                          configuration.terminalCapability);
    if (!initializeRequest.has_value())
    {
        return std::unexpected(initializeRequest.error());
    }
    const QByteArray workingDirectory = configuration.workingDirectory.toUtf8();
    std::expected<QByteArray, QString> openRequest =
        configuration.resumeSessionId.has_value()
            ? m_protocol.resumeSessionRequest(openSessionRequestId, *configuration.resumeSessionId,
                                              workingDirectory.toStdString())
            : m_protocol.newSessionRequest(openSessionRequestId, workingDirectory.toStdString());
    if (!openRequest.has_value())
    {
        return std::unexpected(openRequest.error());
    }

    m_configuration = std::move(configuration);
    m_initializeRequest = std::move(*initializeRequest);
    m_openSessionRequest = std::move(*openRequest);
    m_readyHandler = std::move(readyHandler);
    m_updateHandler = std::move(updateHandler);
    m_requestHandler = std::move(requestHandler);
    m_protocol.reset();
    m_nextRequestId = 3;
    m_process.setProgram(m_configuration.program);
    m_process.setArguments(m_configuration.arguments);
    m_process.setWorkingDirectory(m_configuration.workingDirectory);
    m_process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_state = State::starting;
    m_process.start(QIODevice::ReadWrite);
    m_handshakeDeadline.start(10'000);
    return {};
}

std::expected<std::uint64_t, QString> AcpClient::startPrompt(const std::string_view prompt,
                                                             PromptFinishedHandler finishedHandler)
{
    if (m_state != State::ready)
    {
        return std::unexpected(QStringLiteral("The ACP Agent is not ready for a new prompt."));
    }
    const std::uint64_t requestId = m_nextRequestId++;
    const QByteArray session = m_sessionId.toUtf8();
    const auto request = m_protocol.promptRequest(requestId, session.toStdString(), prompt);
    if (!request.has_value())
    {
        return std::unexpected(request.error());
    }
    if (!write(*request))
    {
        fail(QStringLiteral("The ACP prompt request could not be written."));
        return std::unexpected(QStringLiteral("The ACP prompt request could not be written."));
    }

    m_promptRequestId = requestId;
    m_promptFinishedHandler = std::move(finishedHandler);
    m_pendingAgentRequests.clear();
    m_seenAgentRequests.clear();
    m_cancellationRequested = false;
    m_state = State::promptActive;
    return requestId;
}

std::expected<void, QString> AcpClient::cancelPrompt()
{
    if (m_state != State::promptActive)
    {
        return std::unexpected(QStringLiteral("There is no active ACP prompt to cancel."));
    }
    if (m_cancellationRequested)
    {
        return {};
    }
    const QByteArray session = m_sessionId.toUtf8();
    const auto notification = m_protocol.cancelNotification(session.toStdString());
    if (!notification.has_value() || !write(*notification))
    {
        fail(QStringLiteral("The ACP cancellation request could not be written."));
        return std::unexpected(QStringLiteral("The ACP cancellation request could not be written."));
    }
    m_cancellationRequested = true;
    m_cancelDeadline.start(5'000);
    return {};
}

std::expected<void, QString> AcpClient::completeRequest(const QJsonValue &id, const QJsonValue &result)
{
    const auto response = m_protocol.resultResponse(id, result);
    if (!response.has_value())
    {
        return std::unexpected(response.error());
    }
    return completeRequestImpl(id, *response);
}

std::expected<void, QString> AcpClient::failRequest(const QJsonValue &id, const int code,
                                                    const std::string_view message)
{
    const auto response = m_protocol.errorResponse(id, code, message);
    if (!response.has_value())
    {
        return std::unexpected(response.error());
    }
    return completeRequestImpl(id, *response);
}

void AcpClient::stop()
{
    m_handshakeDeadline.stop();
    m_cancelDeadline.stop();
    const State previousState = m_state;
    m_state = State::stopped;
    if (m_process.state() == QProcess::Running && !m_sessionId.isEmpty()
        && (previousState == State::ready || previousState == State::promptActive))
    {
        if (previousState == State::promptActive)
        {
            const QByteArray session = m_sessionId.toUtf8();
            if (const auto cancel = m_protocol.cancelNotification(session.toStdString()); cancel.has_value())
            {
                static_cast<void>(write(*cancel));
            }
        }
        const bool closeSupported = m_agentCapabilities.value(QStringLiteral("sessionCapabilities"))
                                        .toObject()
                                        .value(QStringLiteral("close"))
                                        .toBool();
        const QByteArray session = m_sessionId.toUtf8();
        if (closeSupported)
        {
            if (const auto close = m_protocol.closeSessionRequest(m_nextRequestId++, session.toStdString());
                close.has_value())
            {
                static_cast<void>(write(*close));
            }
        }
    }
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(1'000))
        {
            m_process.terminate();
            if (!m_process.waitForFinished(500))
            {
                m_process.kill();
                static_cast<void>(m_process.waitForFinished(500));
            }
        }
    }
    m_protocol.reset();
    m_initializeRequest.clear();
    m_openSessionRequest.clear();
    m_pendingAgentRequests.clear();
    m_seenAgentRequests.clear();
    m_promptRequestId.reset();
    m_sessionId.clear();
    m_agentName.clear();
    m_agentVersion.clear();
    m_agentCapabilities = {};
    m_cancellationRequested = false;
    m_readyHandler = {};
    m_updateHandler = {};
    m_requestHandler = {};
    m_promptFinishedHandler = {};
}

AcpClient::State AcpClient::state() const noexcept
{
    return m_state;
}

bool AcpClient::ready() const noexcept
{
    return m_state == State::ready;
}

bool AcpClient::promptActive() const noexcept
{
    return m_state == State::promptActive;
}

QString AcpClient::sessionId() const
{
    return m_sessionId;
}

void AcpClient::handleStarted()
{
    if (m_state != State::starting || m_initializeRequest.isEmpty() || !write(m_initializeRequest))
    {
        fail(QStringLiteral("The ACP initialization request could not be written."));
        return;
    }
    m_state = State::initializing;
}

void AcpClient::handleReadyRead()
{
    const auto messages = m_protocol.append(m_process.readAllStandardOutput());
    if (!messages.has_value())
    {
        fail(messages.error());
        return;
    }
    for (const AcpMessage &message : *messages)
    {
        handleMessage(message);
        if (m_state == State::failed)
        {
            return;
        }
    }
}

void AcpClient::handleMessage(const AcpMessage &message)
{
    switch (message.kind)
    {
        case AcpMessageKind::response:
            handleResponse(message);
            break;
        case AcpMessageKind::notification:
            handleNotification(message);
            break;
        case AcpMessageKind::request:
            handleRequest(message);
            break;
    }
}

void AcpClient::handleResponse(const AcpMessage &message)
{
    if (!message.hasId)
    {
        fail(QStringLiteral("The ACP Agent emitted a response without an id."));
        return;
    }
    if (m_state == State::initializing && matchesRequestId(message.id, initializeRequestId))
    {
        if (!message.error.isEmpty() || !message.result.isObject())
        {
            fail(responseError(message, QStringLiteral("The ACP initialization handshake failed")));
            return;
        }
        const QJsonObject result = message.result.toObject();
        if (result.value(QStringLiteral("protocolVersion")).toInt(-1) != 1)
        {
            fail(QStringLiteral("The ACP Agent does not support protocol version 1."));
            return;
        }
        const QJsonObject agentInfo = result.value(QStringLiteral("agentInfo")).toObject();
        m_agentName = agentInfo.value(QStringLiteral("name")).toString();
        m_agentVersion = agentInfo.value(QStringLiteral("version")).toString();
        m_agentCapabilities = result.value(QStringLiteral("agentCapabilities")).toObject();
        const QJsonObject sessionCapabilities =
            m_agentCapabilities.value(QStringLiteral("sessionCapabilities")).toObject();
        if (m_configuration.resumeSessionId.has_value() && sessionCapabilities.contains(QStringLiteral("resume"))
            && !sessionCapabilities.value(QStringLiteral("resume")).toBool())
        {
            fail(QStringLiteral("The ACP Agent does not support Session resume."));
            return;
        }
        if (!write(m_openSessionRequest))
        {
            fail(QStringLiteral("The ACP session request could not be written."));
            return;
        }
        m_state = State::openingSession;
        return;
    }
    if (m_state == State::openingSession && matchesRequestId(message.id, openSessionRequestId))
    {
        if (!message.error.isEmpty() || !message.result.isObject())
        {
            fail(responseError(message, QStringLiteral("The ACP Agent could not open a session")));
            return;
        }
        const QJsonObject result = message.result.toObject();
        const QString returnedSessionId = result.value(QStringLiteral("sessionId")).toString();
        if (m_configuration.resumeSessionId.has_value())
        {
            m_sessionId = text(*m_configuration.resumeSessionId);
            if (!returnedSessionId.isEmpty() && returnedSessionId != m_sessionId)
            {
                fail(QStringLiteral("The ACP Agent resumed a different session than requested."));
                return;
            }
        }
        else
        {
            m_sessionId = returnedSessionId;
        }
        if (m_sessionId.isEmpty())
        {
            fail(QStringLiteral("The ACP Agent returned a session without an id."));
            return;
        }
        m_handshakeDeadline.stop();
        m_state = State::ready;
        if (m_readyHandler)
        {
            auto handler = std::exchange(m_readyHandler, {});
            handler(AcpClientReady{.sessionId = m_sessionId,
                                   .agentName = m_agentName,
                                   .agentVersion = m_agentVersion,
                                   .agentCapabilities = m_agentCapabilities,
                                   .sessionMetadata = result,
                                   .resumed = m_configuration.resumeSessionId.has_value()});
        }
        return;
    }
    if (m_state == State::promptActive && m_promptRequestId.has_value()
        && matchesRequestId(message.id, *m_promptRequestId))
    {
        m_cancelDeadline.stop();
        if (!m_pendingAgentRequests.isEmpty())
        {
            fail(QStringLiteral("The ACP Agent completed a prompt with client requests still pending."));
            return;
        }
        if (!message.error.isEmpty() || !message.result.isObject())
        {
            auto handler = std::exchange(m_promptFinishedHandler, {});
            m_promptRequestId.reset();
            m_state = State::ready;
            m_cancellationRequested = false;
            if (handler)
            {
                handler(std::unexpected(responseError(message, QStringLiteral("The ACP prompt failed"))));
            }
            return;
        }
        const QString stopReason = message.result.toObject().value(QStringLiteral("stopReason")).toString();
        if (!validStopReason(stopReason) || (m_cancellationRequested && stopReason != QStringLiteral("cancelled")))
        {
            fail(QStringLiteral("The ACP Agent returned an invalid prompt stop reason."));
            return;
        }
        auto handler = std::exchange(m_promptFinishedHandler, {});
        m_promptRequestId.reset();
        m_state = State::ready;
        const bool cancellationRequested = m_cancellationRequested;
        m_cancellationRequested = false;
        if (handler)
        {
            handler(AcpPromptCompletion{.stopReason = stopReason, .cancellationRequested = cancellationRequested});
        }
        return;
    }
    fail(QStringLiteral("The ACP Agent emitted a response for an unknown request."));
}

void AcpClient::handleNotification(const AcpMessage &message)
{
    if (message.method == QStringLiteral("session/update"))
    {
        const QString sessionId = message.params.value(QStringLiteral("sessionId")).toString();
        if ((m_state != State::ready && m_state != State::promptActive) || sessionId != m_sessionId
            || !message.params.value(QStringLiteral("update")).isObject())
        {
            fail(QStringLiteral("An ACP session update violated the owning session contract."));
            return;
        }
    }
    publish(message);
}

void AcpClient::handleRequest(const AcpMessage &message)
{
    if (!message.hasId)
    {
        fail(QStringLiteral("The ACP Agent emitted a request without an id."));
        return;
    }
    const QString key = requestKey(message.id);
    if (key.isEmpty())
    {
        fail(QStringLiteral("The ACP Agent emitted an invalid request id."));
        return;
    }
    if (m_state != State::promptActive || !supportedAgentRequest(message.method))
    {
        const auto rejected = m_protocol.errorResponse(message.id, -32601, "Method not available in this session.");
        if (!rejected.has_value() || !write(*rejected))
        {
            fail(QStringLiteral("The ACP request rejection could not be written."));
        }
        return;
    }
    const QString requestedSessionId = message.params.value(QStringLiteral("sessionId")).toString();
    if ((!requestedSessionId.isEmpty() && requestedSessionId != m_sessionId) || m_seenAgentRequests.contains(key))
    {
        const auto rejected = m_protocol.errorResponse(message.id, -32600, "Duplicate or foreign-session request.");
        if (!rejected.has_value() || !write(*rejected))
        {
            fail(QStringLiteral("The ACP request rejection could not be written."));
        }
        return;
    }
    if (m_pendingAgentRequests.size() >= maximumPendingAgentRequests
        || m_seenAgentRequests.size() >= maximumAgentRequestsPerPrompt)
    {
        fail(QStringLiteral("The ACP Agent emitted excessive client requests."));
        return;
    }

    m_seenAgentRequests.insert(key);
    m_pendingAgentRequests.insert(key, message.id);
    if (!m_requestHandler)
    {
        const auto rejected = failRequest(message.id, -32601, "No ztermy ACP request dispatcher is available.");
        if (!rejected.has_value())
        {
            fail(rejected.error());
        }
        return;
    }
    m_requestHandler(message);
}

void AcpClient::publish(const AcpMessage &message) const
{
    if (m_updateHandler)
    {
        m_updateHandler(message);
    }
}

void AcpClient::fail(const QString &message)
{
    if (m_state == State::failed || m_state == State::stopped)
    {
        return;
    }
    const bool handshaking =
        m_state == State::starting || m_state == State::initializing || m_state == State::openingSession;
    const bool prompting = m_state == State::promptActive;
    m_handshakeDeadline.stop();
    m_cancelDeadline.stop();
    m_state = State::failed;
    m_pendingAgentRequests.clear();
    m_promptRequestId.reset();
    if (handshaking && m_readyHandler)
    {
        auto handler = std::exchange(m_readyHandler, {});
        handler(std::unexpected(message));
    }
    else if (prompting && m_promptFinishedHandler)
    {
        auto handler = std::exchange(m_promptFinishedHandler, {});
        handler(std::unexpected(message));
    }
    else if (m_updateHandler)
    {
        m_updateHandler(std::unexpected(message));
    }
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
    }
}

bool AcpClient::write(const QByteArray &bytes)
{
    return m_process.state() == QProcess::Running && m_process.write(bytes) == bytes.size();
}

std::expected<void, QString> AcpClient::completeRequestImpl(const QJsonValue &id, const QByteArray &response)
{
    const QString key = requestKey(id);
    if (key.isEmpty() || !m_pendingAgentRequests.contains(key))
    {
        return std::unexpected(QStringLiteral("The ACP client request is no longer pending."));
    }
    if (!write(response))
    {
        fail(QStringLiteral("The ACP client request result could not be written."));
        return std::unexpected(QStringLiteral("The ACP client request result could not be written."));
    }
    m_pendingAgentRequests.remove(key);
    return {};
}

} // namespace ztermy::ai
