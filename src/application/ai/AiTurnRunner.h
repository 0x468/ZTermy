#pragma once

#include "core/security/SensitiveByteArray.h"
#include "domain/ai/AiContextCompactor.h"
#include "domain/ai/AiProviderRetryPolicy.h"
#include "domain/ai/AiUsageReporting.h"
#include "infrastructure/ai/ProviderHttpClient.h"

#include <QTimer>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <vector>

namespace ztermy::ai
{

class AiTurnRunner final : public QObject
{
public:
    using TurnId = std::uint64_t;
    using SecretLoader = std::function<std::expected<security::SensitiveByteArray, AiProviderError>()>;
    using EventHandler = std::function<void(TurnId, const AiStreamEvent &)>;
    using FinishedHandler = std::function<void(TurnId, const AiTurnMetrics &)>;
    using RetryHandler = std::function<void(TurnId, std::uint32_t, std::uint64_t)>;
    using JitterSource = std::function<double()>;
    struct ToolHandlingResult final
    {
        std::optional<AiToolOutput> output;
        std::function<void()> cancel;
        bool sideEffecting = false;
    };
    using ToolHandler = std::function<std::expected<ToolHandlingResult, AiProviderError>(const AiToolCall &)>;
    using ToolOutputHandler = std::function<void(const AiToolCall &, const AiToolOutput &)>;

    explicit AiTurnRunner(ProviderHttpClient &client, AiProviderRetryPolicy retryPolicy = AiProviderRetryPolicy{},
                          QObject *parent = nullptr);
    ~AiTurnRunner() override;

    AiTurnRunner(const AiTurnRunner &) = delete;
    AiTurnRunner &operator=(const AiTurnRunner &) = delete;

    [[nodiscard]] std::expected<TurnId, AiProviderError>
    start(AiProviderConfiguration configuration, AiGenerationRequest generation, SecretLoader secretLoader,
          EventHandler eventHandler, FinishedHandler finishedHandler, RetryHandler retryHandler = {},
          JitterSource jitterSource = {}, ToolHandler toolHandler = {}, ToolOutputHandler toolOutputHandler = {});
    [[nodiscard]] bool cancel();
    [[nodiscard]] bool completePendingTool(AiToolOutput output);
    [[nodiscard]] std::optional<AiToolCall> pendingToolCall() const;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] TurnId activeTurnId() const noexcept;

private:
    [[nodiscard]] std::expected<void, AiProviderError> startAttempt();
    void handleEvent(ProviderHttpClient::RequestId requestId, const AiStreamEvent &event);
    void handleFinished(ProviderHttpClient::RequestId requestId);
    [[nodiscard]] std::expected<void, AiProviderError> continuePausedProviderTurn();
    [[nodiscard]] std::expected<void, AiProviderError> continueWithTools();
    [[nodiscard]] std::expected<void, AiProviderError> executeNextTool();
    void observeToolEvent(const AiStreamEvent &event);
    void emitBufferedStart();
    void finishWithError(AiProviderError error);
    void finishTurn();
    void clearTurn();

    ProviderHttpClient &m_client;
    AiProviderRetryPolicy m_retryPolicy;
    QTimer m_retryTimer;
    AiProviderConfiguration m_configuration;
    AiGenerationRequest m_generation;
    SecretLoader m_secretLoader;
    EventHandler m_eventHandler;
    FinishedHandler m_finishedHandler;
    RetryHandler m_retryHandler;
    JitterSource m_jitterSource;
    ToolHandler m_toolHandler;
    ToolOutputHandler m_toolOutputHandler;
    std::optional<ProviderHttpClient::RequestId> m_requestId;
    std::optional<AiStreamEvent> m_bufferedStart;
    std::optional<AiProviderError> m_pendingError;
    AiCompactionLimits m_compactionLimits;
    std::vector<AiToolCall> m_pendingToolCalls;
    std::string m_currentReasoning;
    std::string m_currentReasoningSignature;
    std::string m_providerAssistantContentJson;
    std::optional<AiToolExchange> m_activeToolExchange;
    std::function<void()> m_pendingToolCancellation;
    std::string m_responseId;
    TurnId m_turnId = 0;
    TurnId m_nextTurnId = 1;
    std::uint32_t m_completedRetries = 0;
    std::uint32_t m_completedToolCalls = 0;
    std::uint32_t m_completedPauseContinuations = 0;
    std::size_t m_nextToolIndex = 0;
    std::chrono::steady_clock::time_point m_startedAt;
    std::optional<std::chrono::steady_clock::time_point> m_firstTokenAt;
    bool m_visibleOutputObserved = false;
    bool m_toolContinuationPending = false;
    bool m_pauseContinuationPending = false;
    bool m_waitingForTool = false;
    bool m_irreversibleToolObserved = false;
    bool m_cancelled = false;
};

} // namespace ztermy::ai
