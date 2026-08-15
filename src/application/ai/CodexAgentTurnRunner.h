#pragma once

#include "application/ai/AiTurnRunner.h"
#include "infrastructure/ai/CodexAppServerClient.h"
#include "infrastructure/ai/CodexAppServerEventMapper.h"

#include <QString>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ztermy::ai
{

class CodexAgentTurnRunner final : public QObject
{
public:
    using TurnId = AiTurnRunner::TurnId;
    using ReadyHandler = std::function<void(std::expected<CodexAppServerReady, AiProviderError>)>;
    using EventHandler = AiTurnRunner::EventHandler;
    using FinishedHandler = AiTurnRunner::FinishedHandler;
    using ToolHandler = AiTurnRunner::ToolHandler;
    using ToolOutputHandler = AiTurnRunner::ToolOutputHandler;

    explicit CodexAgentTurnRunner(QObject *parent = nullptr);
    ~CodexAgentTurnRunner() override;

    CodexAgentTurnRunner(const CodexAgentTurnRunner &) = delete;
    CodexAgentTurnRunner &operator=(const CodexAgentTurnRunner &) = delete;

    [[nodiscard]] std::expected<void, AiProviderError> initialize(CodexAppServerConfiguration configuration,
                                                                  ReadyHandler readyHandler);
    [[nodiscard]] std::expected<TurnId, AiProviderError> startConfigured(CodexAppServerConfiguration configuration,
                                                                         std::string prompt, EventHandler eventHandler,
                                                                         FinishedHandler finishedHandler,
                                                                         ToolHandler toolHandler = {},
                                                                         ToolOutputHandler toolOutputHandler = {});
    [[nodiscard]] std::expected<TurnId, AiProviderError> start(std::string_view prompt, EventHandler eventHandler,
                                                               FinishedHandler finishedHandler,
                                                               ToolHandler toolHandler = {},
                                                               ToolOutputHandler toolOutputHandler = {});
    [[nodiscard]] bool cancel();
    [[nodiscard]] bool completePendingTool(const AiToolOutput &output);
    [[nodiscard]] std::optional<AiToolCall> pendingToolCall() const;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] TurnId activeTurnId() const noexcept;
    [[nodiscard]] QString threadId() const;
    void stop();

private:
    struct PendingTool final
    {
        std::uint64_t requestId = 0;
        AiToolCall call;
        std::function<void()> cancel;
    };

    void handleClientEvent(std::expected<CodexAppServerMessage, QString> message);
    void handleToolCall(const CodexDynamicToolCall &call);
    void finishWithError(AiProviderError error);
    void finishTurn();
    void clearTurn();
    [[nodiscard]] AiTurnMetrics metrics() const;

    CodexAppServerClient m_client;
    CodexAppServerEventMapper m_mapper;
    ReadyHandler m_readyHandler;
    EventHandler m_eventHandler;
    FinishedHandler m_finishedHandler;
    ToolHandler m_toolHandler;
    ToolOutputHandler m_toolOutputHandler;
    std::optional<PendingTool> m_pendingTool;
    std::string m_pendingPrompt;
    std::chrono::steady_clock::time_point m_startedAt;
    std::optional<std::chrono::steady_clock::time_point> m_firstTokenAt;
    TurnId m_turnId = 0;
    TurnId m_nextTurnId = 1;
    bool m_initialized = false;
    bool m_startingConfiguredTurn = false;
};

} // namespace ztermy::ai
