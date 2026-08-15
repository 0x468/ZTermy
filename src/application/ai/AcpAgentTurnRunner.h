#pragma once

#include "application/ai/AiTurnRunner.h"
#include "infrastructure/ai/AcpClient.h"
#include "infrastructure/ai/AcpSessionUpdateMapper.h"

#include <QJsonValue>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

enum class AcpTerminalShell : std::uint8_t
{
    posix,
    powerShell,
};

struct AcpTerminalSnapshot final
{
    std::string output;
    std::optional<int> exitCode;
    QString signal;
    bool exited = false;
    bool truncated = false;
};

struct AcpPermissionOption final
{
    QString id;
    QString name;
    QString kind;
};

struct AcpPermissionRequest final
{
    QString toolCallId;
    QString title;
    QString kind;
    QString detailsJson;
    std::vector<AcpPermissionOption> options;
};

class AcpAgentTurnRunner final : public QObject
{
public:
    using TurnId = AiTurnRunner::TurnId;
    using EventHandler = AiTurnRunner::EventHandler;
    using FinishedHandler = AiTurnRunner::FinishedHandler;
    using ToolHandler = AiTurnRunner::ToolHandler;
    using ToolOutputHandler = AiTurnRunner::ToolOutputHandler;
    using TerminalObserver =
        std::function<std::expected<AcpTerminalSnapshot, AiProviderError>(std::string_view commandId)>;
    using PermissionHandler =
        std::function<std::expected<std::optional<QString>, AiProviderError>(const AcpPermissionRequest &request)>;
    using PermissionPendingHandler = std::function<void(const AcpPermissionRequest &request)>;
    using ActivityHandler = std::function<void(const AiToolActivity &activity)>;
    using UsageHandler = std::function<void(const AcpUsageUpdate &usage)>;

    explicit AcpAgentTurnRunner(QObject *parent = nullptr);
    ~AcpAgentTurnRunner() override;

    AcpAgentTurnRunner(const AcpAgentTurnRunner &) = delete;
    AcpAgentTurnRunner &operator=(const AcpAgentTurnRunner &) = delete;

    [[nodiscard]] std::expected<TurnId, AiProviderError>
    startConfigured(AcpClientConfiguration configuration, std::string prompt, AcpTerminalShell shell,
                    EventHandler eventHandler, FinishedHandler finishedHandler, ToolHandler toolHandler,
                    ToolOutputHandler toolOutputHandler, TerminalObserver terminalObserver,
                    PermissionHandler permissionHandler = {}, PermissionPendingHandler permissionPendingHandler = {},
                    ActivityHandler activityHandler = {}, UsageHandler usageHandler = {});
    [[nodiscard]] bool cancel();
    [[nodiscard]] bool completePendingTool(const AiToolOutput &output);
    [[nodiscard]] bool completePendingPermission(const QString &optionId);
    [[nodiscard]] std::optional<AiToolCall> pendingToolCall() const;
    [[nodiscard]] std::optional<AcpPermissionRequest> pendingPermission() const;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] TurnId activeTurnId() const noexcept;
    [[nodiscard]] QString sessionId() const;
    void stop();

private:
    enum class ToolPurpose : std::uint8_t
    {
        create,
        kill,
        release,
    };

    struct TerminalHandle final
    {
        std::string commandId;
        std::size_t outputByteLimit = std::size_t{1024} * 1024;
    };

    struct PendingTool final
    {
        ToolPurpose purpose = ToolPurpose::create;
        QJsonValue requestId;
        AiToolCall call;
        QString terminalId;
        std::size_t outputByteLimit = std::size_t{1024} * 1024;
        std::function<void()> cancel;
    };

    struct PendingPermission final
    {
        QJsonValue requestId;
        AcpPermissionRequest request;
    };

    struct PendingWait final
    {
        QJsonValue requestId;
        QString terminalId;
    };

    void handleUpdate(std::expected<AcpMessage, QString> message);
    void handleRequest(const AcpMessage &message);
    void handleTerminalCreate(const AcpMessage &message);
    void handleTerminalOutput(const AcpMessage &message);
    void handleTerminalWait(const AcpMessage &message);
    void handleTerminalKill(const AcpMessage &message, bool release);
    void handlePermissionRequest(const AcpMessage &message);
    void dispatchTool(PendingTool pending);
    void finishTool(const PendingTool &pending, const AiToolOutput &output);
    void pollTerminalWait();
    void failRequest(const QJsonValue &id, const QString &message);
    void finishWithError(AiProviderError error);
    void finishTurn();
    void clearTurn();
    [[nodiscard]] AiTurnMetrics metrics() const;

    AcpClient m_client;
    AcpSessionUpdateMapper m_mapper;
    QTimer m_terminalWaitTimer;
    EventHandler m_eventHandler;
    FinishedHandler m_finishedHandler;
    ToolHandler m_toolHandler;
    ToolOutputHandler m_toolOutputHandler;
    TerminalObserver m_terminalObserver;
    PermissionHandler m_permissionHandler;
    PermissionPendingHandler m_permissionPendingHandler;
    ActivityHandler m_activityHandler;
    UsageHandler m_usageHandler;
    std::optional<PendingTool> m_pendingTool;
    std::optional<PendingPermission> m_pendingPermission;
    std::optional<PendingWait> m_pendingWait;
    QHash<QString, TerminalHandle> m_terminals;
    std::string m_pendingPrompt;
    std::chrono::steady_clock::time_point m_startedAt;
    std::optional<std::chrono::steady_clock::time_point> m_firstTokenAt;
    TurnId m_turnId = 0;
    TurnId m_nextTurnId = 1;
    std::uint64_t m_nextToolCallId = 1;
    std::uint64_t m_nextTerminalId = 1;
    AcpTerminalShell m_shell = AcpTerminalShell::posix;
    bool m_starting = false;
    bool m_allowNextTerminalCreate = false;
    bool m_alwaysAllowTerminalCreate = false;
};

} // namespace ztermy::ai
