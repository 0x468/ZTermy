#include "infrastructure/ai/McpStdioClient.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>

namespace ztermy::ai
{
namespace
{
constexpr std::uint64_t initializeRequestId = 1;
constexpr std::uint64_t listToolsRequestId = 2;
constexpr qsizetype maximumResultBytes = qsizetype{48} * 1024;

[[nodiscard]] QProcessEnvironment boundedEnvironment()
{
    const QProcessEnvironment source = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment result;
    for (const QString &key :
         {QStringLiteral("PATH"), QStringLiteral("Path"), QStringLiteral("SystemRoot"), QStringLiteral("WINDIR"),
          QStringLiteral("TEMP"), QStringLiteral("TMP"), QStringLiteral("USERPROFILE")})
    {
        if (source.contains(key))
        {
            result.insert(key, source.value(key));
        }
    }
    return result;
}

[[nodiscard]] std::string toolResult(const McpJsonRpcMessage &message)
{
    QJsonObject envelope{{QStringLiteral("ok"), message.error.isEmpty()}, {QStringLiteral("untrusted_evidence"), true}};
    const QJsonObject payload = message.error.isEmpty() ? message.result : message.error;
    const QByteArray serialized = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QJsonValue boundedPayload = payload;
    if (serialized.size() > maximumResultBytes)
    {
        boundedPayload =
            QJsonObject{{QStringLiteral("truncated"), true},
                        {QStringLiteral("original_bytes"), serialized.size()},
                        {QStringLiteral("preview"), QString::fromUtf8(serialized.left(maximumResultBytes))}};
    }
    if (!message.error.isEmpty())
    {
        envelope.insert(QStringLiteral("error"), boundedPayload);
    }
    else
    {
        envelope.insert(QStringLiteral("result"), boundedPayload);
    }
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact).toStdString();
}
} // namespace

McpStdioClient::McpStdioClient(McpToolRegistry &registry, QObject *parent) : QObject(parent), m_registry(registry)
{
    QObject::connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        handleReadyRead();
    });
    QObject::connect(&m_process, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError) {
        fail(QStringLiteral("The MCP server process failed."));
    });
    QObject::connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                     [this](const int, const QProcess::ExitStatus) {
                         if (m_state != State::stopped)
                         {
                             fail(QStringLiteral("The MCP server process exited."));
                         }
                     });
}

McpStdioClient::~McpStdioClient()
{
    stop();
}

std::expected<void, QString> McpStdioClient::start(McpStdioConfiguration configuration,
                                                   DiscoveryHandler discoveryHandler)
{
    stop();
    const QFileInfo executable(configuration.program);
    if (!executable.isAbsolute() || !executable.exists() || !executable.isFile()
        || configuration.identity.trust == McpServerTrust::disabled)
    {
        return std::unexpected(QStringLiteral("An enabled MCP stdio server requires an existing absolute program."));
    }
    if (!configuration.workingDirectory.isEmpty() && !QFileInfo(configuration.workingDirectory).isDir())
    {
        return std::unexpected(QStringLiteral("The MCP working directory does not exist."));
    }
    m_configuration = std::move(configuration);
    m_discoveryHandler = std::move(discoveryHandler);
    m_process.setProgram(m_configuration.program);
    m_process.setArguments(m_configuration.arguments);
    m_process.setWorkingDirectory(m_configuration.workingDirectory);
    m_process.setProcessEnvironment(boundedEnvironment());
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_state = State::initializing;
    m_process.start(QIODevice::ReadWrite);
    if (!m_process.waitForStarted(3'000) || !write(m_protocol.initializeRequest(initializeRequestId)))
    {
        fail(QStringLiteral("The MCP server could not be started."));
        return std::unexpected(QStringLiteral("The MCP server could not be started."));
    }
    return {};
}

std::expected<std::uint64_t, QString> McpStdioClient::call(const std::string_view exposedToolName,
                                                           const std::string_view argumentsJson,
                                                           const CallHandler &handler)
{
    if (m_state != State::ready || m_pendingCalls.size() >= 16)
    {
        return std::unexpected(QStringLiteral("The MCP server is not ready or has too many pending calls."));
    }
    const auto registered = m_registry.resolve(exposedToolName);
    if (!registered.has_value() || registered->serverId != m_configuration.identity.id)
    {
        return std::unexpected(QStringLiteral("The MCP tool is not approved for this server."));
    }
    const std::uint64_t id = m_nextRequestId++;
    auto request = m_protocol.callToolRequest(id, registered->remoteName, argumentsJson);
    if (!request.has_value())
    {
        return std::unexpected(request.error());
    }
    m_pendingCalls.insert(id, handler);
    if (!write(*request))
    {
        m_pendingCalls.remove(id);
        return std::unexpected(QStringLiteral("The MCP tool request could not be written."));
    }
    return id;
}

bool McpStdioClient::cancel(const std::uint64_t requestId, const std::string_view reason)
{
    const auto handler = m_pendingCalls.take(requestId);
    if (!handler)
    {
        return false;
    }
    static_cast<void>(write(m_protocol.cancelRequestNotification(requestId, reason)));
    handler(std::unexpected(QStringLiteral("The MCP tool call was cancelled.")));
    return true;
}

void McpStdioClient::stop()
{
    m_state = State::stopped;
    m_registry.disableServer(m_configuration.identity.id);
    const auto pendingHandlers = m_pendingCalls.values();
    m_pendingCalls.clear();
    for (const auto &handler : pendingHandlers)
    {
        if (handler)
        {
            handler(std::unexpected(QStringLiteral("The MCP server stopped.")));
        }
    }
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.terminate();
        if (!m_process.waitForFinished(1'000))
        {
            m_process.kill();
            static_cast<void>(m_process.waitForFinished(1'000));
        }
    }
}

bool McpStdioClient::ready() const noexcept
{
    return m_state == State::ready;
}

void McpStdioClient::handleReadyRead()
{
    auto messages = m_protocol.append(m_process.readAllStandardOutput());
    if (!messages.has_value())
    {
        fail(messages.error());
        return;
    }
    for (const auto &message : *messages)
    {
        handleMessage(message);
    }
}

void McpStdioClient::handleMessage(const McpJsonRpcMessage &message)
{
    if (!message.id.has_value())
    {
        return;
    }
    if (m_state == State::initializing && *message.id == initializeRequestId)
    {
        if (!message.error.isEmpty() || !write(m_protocol.initializedNotification())
            || !write(m_protocol.listToolsRequest(listToolsRequestId)))
        {
            fail(QStringLiteral("The MCP initialize handshake failed."));
            return;
        }
        m_state = State::listing;
        return;
    }
    if (m_state == State::listing && *message.id == listToolsRequestId)
    {
        auto discovered = McpJsonRpcProtocol::discoveredTools(message);
        if (!discovered.has_value())
        {
            fail(discovered.error());
            return;
        }
        auto update = m_registry.update(m_configuration.identity, *discovered);
        if (!update.has_value())
        {
            fail(QString::fromUtf8(update.error()));
            return;
        }
        m_state = State::ready;
        if (m_discoveryHandler)
        {
            m_discoveryHandler(std::move(*update));
        }
        return;
    }
    const auto handler = m_pendingCalls.take(*message.id);
    if (handler)
    {
        handler(toolResult(message));
    }
}

void McpStdioClient::fail(const QString &message)
{
    if (m_state == State::failed || m_state == State::stopped)
    {
        return;
    }
    m_state = State::failed;
    m_registry.disableServer(m_configuration.identity.id);
    if (m_discoveryHandler)
    {
        m_discoveryHandler(std::unexpected(message));
    }
    const auto pendingHandlers = m_pendingCalls.values();
    m_pendingCalls.clear();
    for (const auto &handler : pendingHandlers)
    {
        if (handler)
        {
            handler(std::unexpected(message));
        }
    }
}

bool McpStdioClient::write(const QByteArray &bytes)
{
    return m_process.state() == QProcess::Running && m_process.write(bytes) == bytes.size();
}

} // namespace ztermy::ai
