#include "infrastructure/ai/CodexAppServerClient.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>
#include <ranges>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maximumArgumentsBytes = qsizetype{64} * 1024;
constexpr qsizetype maximumStderrBytes = qsizetype{64} * 1024;
constexpr qsizetype maximumPendingToolCalls = 16;

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString nestedId(const QJsonObject &object, const QString &container)
{
    return object.value(container).toObject().value(QStringLiteral("id")).toString();
}

} // namespace

CodexAppServerClient::CodexAppServerClient(QObject *parent) : QObject(parent)
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
        fail(QStringLiteral("The Codex app-server process failed: %1").arg(m_process.errorString()));
    });
    QObject::connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                     [this](const int exitCode, const QProcess::ExitStatus) {
                         if (m_state != State::stopped && m_state != State::failed)
                         {
                             fail(QStringLiteral("The Codex app-server exited unexpectedly (code %1).").arg(exitCode));
                         }
                     });
    m_handshakeDeadline.setSingleShot(true);
    QObject::connect(&m_handshakeDeadline, &QTimer::timeout, this, [this] {
        if (m_state == State::starting || m_state == State::initializing || m_state == State::openingThread)
        {
            fail(QStringLiteral("The Codex app-server did not complete its handshake in time."));
        }
    });
}

CodexAppServerClient::~CodexAppServerClient()
{
    stop();
}

std::expected<void, QString> CodexAppServerClient::start(CodexAppServerConfiguration configuration,
                                                         ReadyHandler readyHandler, EventHandler eventHandler,
                                                         ToolCallHandler toolCallHandler)
{
    stop();
    const QFileInfo executable(configuration.program);
    if (!configuration.dynamicToolsVerified)
    {
        return std::unexpected(QStringLiteral("This Codex app-server has not verified dynamic-tool support."));
    }
    if (!executable.isAbsolute() || !executable.exists() || !executable.isFile())
    {
        return std::unexpected(QStringLiteral("Codex requires an existing absolute executable path."));
    }
    if (!QDir::isAbsolutePath(configuration.workingDirectory) || !QFileInfo(configuration.workingDirectory).isDir()
        || configuration.clientVersion.empty())
    {
        return std::unexpected(QStringLiteral("Codex requires a valid absolute working directory and client version."));
    }

    std::expected<QByteArray, QString> openRequest =
        configuration.resumeThreadId.has_value()
            ? m_protocol.resumeThreadRequest(openThreadRequestId, *configuration.resumeThreadId, configuration.model,
                                             configuration.workingDirectory.toUtf8().toStdString(),
                                             configuration.developerInstructions, configuration.tools)
            : m_protocol.startThreadRequest(openThreadRequestId, configuration.model,
                                            configuration.workingDirectory.toUtf8().toStdString(),
                                            configuration.developerInstructions, configuration.tools);
    if (!openRequest.has_value())
    {
        return std::unexpected(openRequest.error());
    }

    m_configuration = std::move(configuration);
    m_openThreadRequest = std::move(*openRequest);
    m_readyHandler = std::move(readyHandler);
    m_eventHandler = std::move(eventHandler);
    m_toolCallHandler = std::move(toolCallHandler);
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

std::expected<std::uint64_t, QString> CodexAppServerClient::startTurn(const std::string_view prompt)
{
    if (m_state != State::ready)
    {
        return std::unexpected(QStringLiteral("The Codex Agent is not ready for a new turn."));
    }
    const std::uint64_t requestId = m_nextRequestId++;
    const auto request = m_protocol.startTurnRequest(requestId, m_threadId.toUtf8().toStdString(), prompt);
    if (!request.has_value())
    {
        return std::unexpected(request.error());
    }
    if (!write(*request))
    {
        fail(QStringLiteral("The Codex turn request could not be written."));
        return std::unexpected(QStringLiteral("The Codex turn request could not be written."));
    }
    m_turnStartRequestId = requestId;
    m_activeTurnId.clear();
    m_interruptRequested = false;
    m_state = State::turnActive;
    return requestId;
}

std::expected<void, QString> CodexAppServerClient::interrupt()
{
    if (m_state != State::turnActive)
    {
        return std::unexpected(QStringLiteral("There is no active Codex turn to interrupt."));
    }
    if (m_interruptRequested)
    {
        return {};
    }
    m_interruptRequested = true;
    if (m_activeTurnId.isEmpty())
    {
        return {};
    }
    return sendInterrupt();
}

std::expected<void, QString> CodexAppServerClient::completeToolCall(const std::uint64_t requestId, const bool success,
                                                                    const std::string_view output)
{
    if (!m_pendingToolCalls.contains(requestId))
    {
        return std::unexpected(QStringLiteral("The Codex dynamic tool call is no longer pending."));
    }
    const auto response = m_protocol.dynamicToolResponse(requestId, success, output);
    if (!response.has_value())
    {
        return std::unexpected(response.error());
    }
    if (!write(*response))
    {
        fail(QStringLiteral("The Codex dynamic tool result could not be written."));
        return std::unexpected(QStringLiteral("The Codex dynamic tool result could not be written."));
    }
    m_pendingToolCalls.remove(requestId);
    return {};
}

void CodexAppServerClient::stop()
{
    m_handshakeDeadline.stop();
    m_state = State::stopped;
    m_pendingToolCalls.clear();
    m_turnStartRequestId.reset();
    m_threadId.clear();
    m_activeTurnId.clear();
    m_interruptRequested = false;
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
        static_cast<void>(m_process.waitForFinished(1'000));
    }
    m_readyHandler = {};
    m_eventHandler = {};
    m_toolCallHandler = {};
}

CodexAppServerClient::State CodexAppServerClient::state() const noexcept
{
    return m_state;
}

bool CodexAppServerClient::ready() const noexcept
{
    return m_state == State::ready;
}

bool CodexAppServerClient::turnActive() const noexcept
{
    return m_state == State::turnActive;
}

QString CodexAppServerClient::threadId() const
{
    return m_threadId;
}

void CodexAppServerClient::handleStarted()
{
    if (m_state != State::starting
        || !write(m_protocol.initializeRequest(initializeRequestId, m_configuration.clientVersion)))
    {
        fail(QStringLiteral("The Codex app-server initialization request could not be written."));
        return;
    }
    m_state = State::initializing;
}

void CodexAppServerClient::handleReadyRead()
{
    const auto messages = m_protocol.append(m_process.readAllStandardOutput());
    if (!messages.has_value())
    {
        fail(messages.error());
        return;
    }
    for (const CodexAppServerMessage &message : *messages)
    {
        handleMessage(message);
        if (m_state == State::failed)
        {
            return;
        }
    }
}

void CodexAppServerClient::handleMessage(const CodexAppServerMessage &message)
{
    switch (message.kind)
    {
        case CodexAppServerMessageKind::response:
            handleResponse(message);
            break;
        case CodexAppServerMessageKind::notification:
            handleNotification(message);
            break;
        case CodexAppServerMessageKind::request:
            handleRequest(message);
            break;
    }
}

void CodexAppServerClient::handleResponse(const CodexAppServerMessage &message)
{
    if (!message.id.has_value())
    {
        fail(QStringLiteral("The Codex app-server emitted a response without an id."));
        return;
    }
    if (m_state == State::initializing && *message.id == initializeRequestId)
    {
        if (!message.error.isEmpty() || !write(m_protocol.initializedNotification()) || !write(m_openThreadRequest))
        {
            fail(QStringLiteral("The Codex app-server initialize handshake failed."));
            return;
        }
        m_state = State::openingThread;
        return;
    }
    if (m_state == State::openingThread && *message.id == openThreadRequestId)
    {
        m_threadId = nestedId(message.result, QStringLiteral("thread"));
        if (!message.error.isEmpty() || m_threadId.isEmpty())
        {
            fail(QStringLiteral("The Codex app-server could not open a thread."));
            return;
        }
        m_handshakeDeadline.stop();
        m_state = State::ready;
        if (m_readyHandler)
        {
            auto handler = std::move(m_readyHandler);
            handler(CodexAppServerReady{.threadId = m_threadId, .resumed = m_configuration.resumeThreadId.has_value()});
        }
        return;
    }
    if (m_turnStartRequestId == message.id)
    {
        m_turnStartRequestId.reset();
        if (!message.error.isEmpty())
        {
            m_state = State::ready;
            m_interruptRequested = false;
            publish(message);
            return;
        }
        m_activeTurnId = nestedId(message.result, QStringLiteral("turn"));
        if (m_activeTurnId.isEmpty())
        {
            fail(QStringLiteral("The Codex app-server returned a turn without an id."));
            return;
        }
        publish(message);
        if (m_interruptRequested)
        {
            const auto interrupted = sendInterrupt();
            if (!interrupted.has_value())
            {
                fail(interrupted.error());
            }
        }
        return;
    }
    publish(message);
}

void CodexAppServerClient::handleNotification(const CodexAppServerMessage &message)
{
    const QString scopedThread = message.params.value(QStringLiteral("threadId")).toString();
    if (!scopedThread.isEmpty() && scopedThread != m_threadId)
    {
        fail(QStringLiteral("The Codex app-server emitted an event for another terminal thread."));
        return;
    }
    if (message.method == QStringLiteral("turn/started"))
    {
        const QString turnId = nestedId(message.params, QStringLiteral("turn"));
        if (!turnId.isEmpty())
        {
            m_activeTurnId = turnId;
        }
    }
    else if (message.method == QStringLiteral("turn/completed"))
    {
        m_state = State::ready;
        m_turnStartRequestId.reset();
        m_activeTurnId.clear();
        m_interruptRequested = false;
        m_pendingToolCalls.clear();
    }
    publish(message);
}

void CodexAppServerClient::handleRequest(const CodexAppServerMessage &message)
{
    if (message.method != QStringLiteral("item/tool/call") || !message.id.has_value()
        || m_pendingToolCalls.contains(message.id.value_or(0)) || m_pendingToolCalls.size() >= maximumPendingToolCalls)
    {
        fail(QStringLiteral("The Codex app-server emitted an unsupported or excessive client request."));
        return;
    }
    const QString threadId = message.params.value(QStringLiteral("threadId")).toString();
    const QString turnId = message.params.value(QStringLiteral("turnId")).toString();
    const QString callId = message.params.value(QStringLiteral("callId")).toString();
    const QString tool = message.params.value(QStringLiteral("tool")).toString();
    const QJsonValue arguments = message.params.value(QStringLiteral("arguments"));
    const bool knownTool = std::ranges::any_of(m_configuration.tools, [&tool](const AiToolDefinition &definition) {
        return text(definition.name) == tool;
    });
    const QByteArray argumentsJson = QJsonDocument(arguments.toObject()).toJson(QJsonDocument::Compact);
    if (m_state != State::turnActive || threadId != m_threadId || turnId.isEmpty()
        || (!m_activeTurnId.isEmpty() && turnId != m_activeTurnId) || callId.isEmpty() || !knownTool
        || !arguments.isObject() || argumentsJson.size() > maximumArgumentsBytes)
    {
        fail(QStringLiteral("The Codex dynamic tool request violated the owning turn contract."));
        return;
    }

    m_pendingToolCalls.insert(*message.id);
    publish(message);
    if (!m_toolCallHandler)
    {
        const auto completed = completeToolCall(*message.id, false, "No ztermy tool dispatcher is available.");
        if (!completed.has_value())
        {
            fail(completed.error());
        }
        return;
    }
    m_toolCallHandler(CodexDynamicToolCall{.requestId = *message.id,
                                           .threadId = threadId,
                                           .turnId = turnId,
                                           .callId = callId,
                                           .tool = tool,
                                           .argumentsJson = argumentsJson.toStdString()});
}

void CodexAppServerClient::publish(const CodexAppServerMessage &message) const
{
    if (m_eventHandler)
    {
        m_eventHandler(message);
    }
}

void CodexAppServerClient::fail(const QString &message)
{
    if (m_state == State::failed || m_state == State::stopped)
    {
        return;
    }
    m_handshakeDeadline.stop();
    m_state = State::failed;
    m_pendingToolCalls.clear();
    m_turnStartRequestId.reset();
    m_activeTurnId.clear();
    if (m_readyHandler)
    {
        auto handler = std::move(m_readyHandler);
        handler(std::unexpected(message));
    }
    else if (m_eventHandler)
    {
        m_eventHandler(std::unexpected(message));
    }
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
    }
}

bool CodexAppServerClient::write(const QByteArray &bytes)
{
    return m_process.state() == QProcess::Running && m_process.write(bytes) == bytes.size();
}

std::expected<void, QString> CodexAppServerClient::sendInterrupt()
{
    const std::uint64_t requestId = m_nextRequestId++;
    const auto request = m_protocol.interruptTurnRequest(requestId, m_threadId.toUtf8().toStdString(),
                                                         m_activeTurnId.toUtf8().toStdString());
    if (!request.has_value())
    {
        return std::unexpected(request.error());
    }
    if (!write(*request))
    {
        return std::unexpected(QStringLiteral("The Codex interrupt request could not be written."));
    }
    return {};
}

} // namespace ztermy::ai
