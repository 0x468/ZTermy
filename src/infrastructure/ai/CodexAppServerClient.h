#pragma once

#include "domain/ai/AiProviderTypes.h"
#include "infrastructure/ai/CodexAppServerProtocol.h"

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
#include <vector>

namespace ztermy::ai
{

struct CodexAppServerConfiguration final
{
    QString program;
    QStringList arguments;
    QString workingDirectory;
    std::string model;
    std::string clientVersion;
    std::string developerInstructions;
    std::vector<AiToolDefinition> tools;
    std::optional<std::string> resumeThreadId;
    bool dynamicToolsVerified = false;
};

struct CodexAppServerReady final
{
    QString threadId;
    bool resumed = false;
};

struct CodexDynamicToolCall final
{
    std::uint64_t requestId = 0;
    QString threadId;
    QString turnId;
    QString callId;
    QString tool;
    std::string argumentsJson;
};

class CodexAppServerClient final : public QObject
{
public:
    enum class State : std::uint8_t
    {
        stopped,
        starting,
        initializing,
        openingThread,
        ready,
        turnActive,
        failed,
    };

    using ReadyHandler = std::function<void(std::expected<CodexAppServerReady, QString>)>;
    using EventHandler = std::function<void(std::expected<CodexAppServerMessage, QString>)>;
    using ToolCallHandler = std::function<void(const CodexDynamicToolCall &)>;

    explicit CodexAppServerClient(QObject *parent = nullptr);
    ~CodexAppServerClient() override;

    CodexAppServerClient(const CodexAppServerClient &) = delete;
    CodexAppServerClient &operator=(const CodexAppServerClient &) = delete;

    [[nodiscard]] std::expected<void, QString> start(CodexAppServerConfiguration configuration,
                                                     ReadyHandler readyHandler, EventHandler eventHandler,
                                                     ToolCallHandler toolCallHandler);
    [[nodiscard]] std::expected<std::uint64_t, QString> startTurn(std::string_view prompt);
    [[nodiscard]] std::expected<void, QString> interrupt();
    [[nodiscard]] std::expected<void, QString> completeToolCall(std::uint64_t requestId, bool success,
                                                                std::string_view output);
    void stop();

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool turnActive() const noexcept;
    [[nodiscard]] QString threadId() const;

private:
    void handleStarted();
    void handleReadyRead();
    void handleMessage(const CodexAppServerMessage &message);
    void handleResponse(const CodexAppServerMessage &message);
    void handleNotification(const CodexAppServerMessage &message);
    void handleRequest(const CodexAppServerMessage &message);
    void publish(const CodexAppServerMessage &message) const;
    void fail(const QString &message);
    [[nodiscard]] bool write(const QByteArray &bytes);
    [[nodiscard]] std::expected<void, QString> sendInterrupt();

    static constexpr std::uint64_t initializeRequestId = 1;
    static constexpr std::uint64_t openThreadRequestId = 2;

    QProcess m_process;
    QTimer m_handshakeDeadline;
    CodexAppServerProtocol m_protocol;
    CodexAppServerConfiguration m_configuration;
    QByteArray m_openThreadRequest;
    ReadyHandler m_readyHandler;
    EventHandler m_eventHandler;
    ToolCallHandler m_toolCallHandler;
    QSet<std::uint64_t> m_pendingToolCalls;
    std::optional<std::uint64_t> m_turnStartRequestId;
    QString m_threadId;
    QString m_activeTurnId;
    std::uint64_t m_nextRequestId = 3;
    State m_state = State::stopped;
    bool m_interruptRequested = false;
};

} // namespace ztermy::ai
