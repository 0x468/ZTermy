#pragma once

#include "domain/ai/McpToolRegistry.h"
#include "infrastructure/ai/McpJsonRpcProtocol.h"

#include <QHash>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <functional>

namespace ztermy::ai
{

struct McpStdioConfiguration final
{
    McpServerIdentity identity;
    QString program;
    QStringList arguments;
    QString workingDirectory;
};

class McpStdioClient final : public QObject
{
public:
    using DiscoveryHandler = std::function<void(std::expected<McpDiscoveryUpdate, QString>)>;
    using CallHandler = std::function<void(std::expected<std::string, QString>)>;

    explicit McpStdioClient(McpToolRegistry &registry, QObject *parent = nullptr);
    ~McpStdioClient() override;

    McpStdioClient(const McpStdioClient &) = delete;
    McpStdioClient &operator=(const McpStdioClient &) = delete;

    [[nodiscard]] std::expected<void, QString> start(McpStdioConfiguration configuration,
                                                     DiscoveryHandler discoveryHandler);
    [[nodiscard]] std::expected<std::uint64_t, QString>
    call(std::string_view exposedToolName, std::string_view argumentsJson, const CallHandler &handler);
    [[nodiscard]] bool cancel(std::uint64_t requestId, std::string_view reason = "Cancelled by the user.");
    void stop();
    [[nodiscard]] bool ready() const noexcept;

private:
    enum class State : std::uint8_t
    {
        stopped,
        initializing,
        listing,
        ready,
        failed
    };

    struct PendingCall final
    {
        CallHandler handler;
        QTimer *deadline = nullptr;
    };

    void handleReadyRead();
    void handleMessage(const McpJsonRpcMessage &message);
    void fail(const QString &message);
    [[nodiscard]] bool write(const QByteArray &bytes);
    void armHandshakeDeadline();

    McpToolRegistry &m_registry;
    QProcess m_process;
    McpJsonRpcProtocol m_protocol;
    McpStdioConfiguration m_configuration;
    DiscoveryHandler m_discoveryHandler;
    QHash<std::uint64_t, PendingCall> m_pendingCalls;
    QTimer m_handshakeDeadline;
    std::uint64_t m_nextRequestId = 3;
    State m_state = State::stopped;
};

} // namespace ztermy::ai
