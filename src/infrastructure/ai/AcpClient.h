#pragma once

#include "infrastructure/ai/AcpProtocol.h"

#include <QHash>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ztermy::ai
{

struct AcpClientConfiguration final
{
    QString program;
    QStringList arguments;
    QString workingDirectory;
    std::string clientVersion;
    std::optional<std::string> resumeSessionId;
    bool terminalCapability = true;
};

struct AcpClientReady final
{
    QString sessionId;
    QString agentName;
    QString agentVersion;
    QJsonObject agentCapabilities;
    QJsonObject sessionMetadata;
    bool resumed = false;
};

struct AcpPromptCompletion final
{
    QString stopReason;
    bool cancellationRequested = false;
};

class AcpClient final : public QObject
{
public:
    enum class State : std::uint8_t
    {
        stopped,
        starting,
        initializing,
        openingSession,
        ready,
        promptActive,
        failed,
    };

    using ReadyHandler = std::function<void(std::expected<AcpClientReady, QString>)>;
    using UpdateHandler = std::function<void(std::expected<AcpMessage, QString>)>;
    using RequestHandler = std::function<void(const AcpMessage &)>;
    using PromptFinishedHandler = std::function<void(std::expected<AcpPromptCompletion, QString>)>;

    explicit AcpClient(QObject *parent = nullptr);
    ~AcpClient() override;

    AcpClient(const AcpClient &) = delete;
    AcpClient &operator=(const AcpClient &) = delete;

    [[nodiscard]] std::expected<void, QString> start(AcpClientConfiguration configuration, ReadyHandler readyHandler,
                                                     UpdateHandler updateHandler, RequestHandler requestHandler);
    [[nodiscard]] std::expected<std::uint64_t, QString> startPrompt(std::string_view prompt,
                                                                    PromptFinishedHandler finishedHandler);
    [[nodiscard]] std::expected<void, QString> cancelPrompt();
    [[nodiscard]] std::expected<void, QString> completeRequest(const QJsonValue &id, const QJsonValue &result);
    [[nodiscard]] std::expected<void, QString> failRequest(const QJsonValue &id, int code, std::string_view message);
    void stop();

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool promptActive() const noexcept;
    [[nodiscard]] QString sessionId() const;

private:
    void handleStarted();
    void handleReadyRead();
    void handleMessage(const AcpMessage &message);
    void handleResponse(const AcpMessage &message);
    void handleNotification(const AcpMessage &message);
    void handleRequest(const AcpMessage &message);
    void publish(const AcpMessage &message) const;
    void fail(const QString &message);
    [[nodiscard]] bool write(const QByteArray &bytes);
    [[nodiscard]] std::expected<void, QString> completeRequestImpl(const QJsonValue &id, const QByteArray &response);

    static constexpr std::uint64_t initializeRequestId = 1;
    static constexpr std::uint64_t openSessionRequestId = 2;

    QProcess m_process;
    QTimer m_handshakeDeadline;
    QTimer m_cancelDeadline;
    AcpProtocol m_protocol;
    AcpClientConfiguration m_configuration;
    QByteArray m_initializeRequest;
    QByteArray m_openSessionRequest;
    ReadyHandler m_readyHandler;
    UpdateHandler m_updateHandler;
    RequestHandler m_requestHandler;
    PromptFinishedHandler m_promptFinishedHandler;
    QHash<QString, QJsonValue> m_pendingAgentRequests;
    QSet<QString> m_seenAgentRequests;
    std::optional<std::uint64_t> m_promptRequestId;
    QString m_sessionId;
    QString m_agentName;
    QString m_agentVersion;
    QJsonObject m_agentCapabilities;
    std::uint64_t m_nextRequestId = 3;
    State m_state = State::stopped;
    bool m_cancellationRequested = false;
};

} // namespace ztermy::ai
