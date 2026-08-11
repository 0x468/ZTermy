#pragma once

#include "core/security/SensitiveByteArray.h"
#include "domain/ai/AiProviderRetryPolicy.h"
#include "infrastructure/ai/ProviderHttpClient.h"

#include <QTimer>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>

namespace ztermy::ai
{

class AiTurnRunner final : public QObject
{
public:
    using TurnId = std::uint64_t;
    using SecretLoader = std::function<std::expected<security::SensitiveByteArray, AiProviderError>()>;
    using EventHandler = std::function<void(TurnId, const AiStreamEvent &)>;
    using FinishedHandler = std::function<void(TurnId)>;
    using RetryHandler = std::function<void(TurnId, std::uint32_t, std::uint64_t)>;
    using JitterSource = std::function<double()>;

    explicit AiTurnRunner(ProviderHttpClient &client, AiProviderRetryPolicy retryPolicy = AiProviderRetryPolicy{},
                          QObject *parent = nullptr);
    ~AiTurnRunner() override;

    AiTurnRunner(const AiTurnRunner &) = delete;
    AiTurnRunner &operator=(const AiTurnRunner &) = delete;

    [[nodiscard]] std::expected<TurnId, AiProviderError>
    start(AiProviderConfiguration configuration, AiGenerationRequest generation, SecretLoader secretLoader,
          EventHandler eventHandler, FinishedHandler finishedHandler, RetryHandler retryHandler = {},
          JitterSource jitterSource = {});
    [[nodiscard]] bool cancel();
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] TurnId activeTurnId() const noexcept;

private:
    [[nodiscard]] std::expected<void, AiProviderError> startAttempt();
    void handleEvent(ProviderHttpClient::RequestId requestId, const AiStreamEvent &event);
    void handleFinished(ProviderHttpClient::RequestId requestId);
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
    std::optional<ProviderHttpClient::RequestId> m_requestId;
    std::optional<AiStreamEvent> m_bufferedStart;
    std::optional<AiProviderError> m_pendingError;
    TurnId m_turnId = 0;
    TurnId m_nextTurnId = 1;
    std::uint32_t m_completedRetries = 0;
    bool m_visibleOutputObserved = false;
    bool m_cancelled = false;
};

} // namespace ztermy::ai
