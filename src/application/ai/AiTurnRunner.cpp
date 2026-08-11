#include "application/ai/AiTurnRunner.h"

#include <QPointer>
#include <QRandomGenerator>

#include <algorithm>
#include <limits>
#include <utility>

namespace ztermy::ai
{

AiTurnRunner::AiTurnRunner(ProviderHttpClient &client,
                           AiProviderRetryPolicy retryPolicy,
                           QObject *parent)
    : QObject(parent), m_client(client), m_retryPolicy(retryPolicy)
{
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        if (!active() || m_cancelled)
        {
            return;
        }
        const auto started = startAttempt();
        if (!started.has_value())
        {
            finishWithError(started.error());
        }
    });
}

AiTurnRunner::~AiTurnRunner()
{
    m_retryTimer.stop();
    if (m_requestId.has_value())
    {
        static_cast<void>(m_client.cancel(*m_requestId));
    }
}

std::expected<AiTurnRunner::TurnId, AiProviderError>
AiTurnRunner::start(AiProviderConfiguration configuration,
                    AiGenerationRequest generation,
                    SecretLoader secretLoader,
                    EventHandler eventHandler,
                    FinishedHandler finishedHandler,
                    RetryHandler retryHandler,
                    JitterSource jitterSource)
{
    if (active())
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                               .message = "An AI turn is already active.",
                                               .retryable = false});
    }
    if (!secretLoader || !eventHandler || !finishedHandler)
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                               .message = "The AI turn callbacks are incomplete.",
                                               .retryable = false});
    }

    m_configuration = std::move(configuration);
    m_generation = std::move(generation);
    m_secretLoader = std::move(secretLoader);
    m_eventHandler = std::move(eventHandler);
    m_finishedHandler = std::move(finishedHandler);
    m_retryHandler = std::move(retryHandler);
    m_jitterSource = std::move(jitterSource);
    m_turnId = m_nextTurnId++;
    m_completedRetries = 0;
    m_visibleOutputObserved = false;
    m_cancelled = false;

    const auto turnId = m_turnId;
    const auto started = startAttempt();
    if (!started.has_value())
    {
        clearTurn();
        return std::unexpected(started.error());
    }
    return turnId;
}

bool AiTurnRunner::cancel()
{
    if (!active())
    {
        return false;
    }
    m_cancelled = true;
    if (m_retryTimer.isActive())
    {
        m_retryTimer.stop();
        finishWithError(AiProviderError{.code = AiProviderErrorCode::cancelled,
                                        .message = "AI request cancelled.",
                                        .retryable = false});
        return true;
    }
    return m_requestId.has_value() && m_client.cancel(*m_requestId);
}

bool AiTurnRunner::active() const noexcept
{
    return m_turnId != 0;
}

AiTurnRunner::TurnId AiTurnRunner::activeTurnId() const noexcept
{
    return m_turnId;
}

std::expected<void, AiProviderError> AiTurnRunner::startAttempt()
{
    m_bufferedStart.reset();
    m_pendingError.reset();
    m_visibleOutputObserved = false;

    auto secret = m_secretLoader();
    if (!secret.has_value())
    {
        return std::unexpected(secret.error());
    }

    const QPointer<AiTurnRunner> guardedThis(this);
    const auto started = m_client.start(
        m_configuration,
        m_generation,
        std::move(secret.value()),
        [guardedThis](const auto requestId, const AiStreamEvent &event) {
            if (guardedThis)
            {
                guardedThis->handleEvent(requestId, event);
            }
        },
        [guardedThis](const auto requestId) {
            if (guardedThis)
            {
                guardedThis->handleFinished(requestId);
            }
        });
    if (!started.has_value())
    {
        return std::unexpected(started.error());
    }
    m_requestId = *started;
    return {};
}

void AiTurnRunner::handleEvent(const ProviderHttpClient::RequestId requestId,
                               const AiStreamEvent &event)
{
    if (!m_requestId.has_value() || *m_requestId != requestId || !active())
    {
        return;
    }
    if (event.type == AiStreamEventType::responseStarted)
    {
        m_bufferedStart = event;
        return;
    }
    if (event.type == AiStreamEventType::responseFailed)
    {
        m_pendingError = event.error.value_or(
            AiProviderError{.code = AiProviderErrorCode::protocol,
                            .message = "Provider failed without an error payload.",
                            .retryable = false});
        return;
    }

    emitBufferedStart();
    m_visibleOutputObserved = true;
    m_eventHandler(m_turnId, event);
}

void AiTurnRunner::handleFinished(const ProviderHttpClient::RequestId requestId)
{
    if (!m_requestId.has_value() || *m_requestId != requestId || !active())
    {
        return;
    }
    m_requestId.reset();

    if (!m_pendingError.has_value())
    {
        emitBufferedStart();
        finishTurn();
        return;
    }

    const auto error = *m_pendingError;
    const auto jitter = m_jitterSource ? m_jitterSource()
                                       : QRandomGenerator::global()->generateDouble();
    const auto decision = m_retryPolicy.decide(error, m_completedRetries, jitter);
    if (!m_cancelled && !m_visibleOutputObserved && decision.retry)
    {
        ++m_completedRetries;
        m_bufferedStart.reset();
        m_pendingError.reset();
        m_retryTimer.start(static_cast<int>(std::min<std::uint64_t>(
            decision.delayMilliseconds,
            static_cast<std::uint64_t>(std::numeric_limits<int>::max()))));
        if (m_retryHandler)
        {
            m_retryHandler(m_turnId, m_completedRetries, decision.delayMilliseconds);
        }
        return;
    }
    finishWithError(error);
}

void AiTurnRunner::emitBufferedStart()
{
    if (!m_bufferedStart.has_value())
    {
        return;
    }
    m_eventHandler(m_turnId, *m_bufferedStart);
    m_bufferedStart.reset();
}

void AiTurnRunner::finishWithError(AiProviderError error)
{
    if (!active())
    {
        return;
    }
    emitBufferedStart();
    m_eventHandler(m_turnId,
                   AiStreamEvent{.type = AiStreamEventType::responseFailed,
                                 .error = std::move(error)});
    finishTurn();
}

void AiTurnRunner::finishTurn()
{
    const auto turnId = m_turnId;
    auto finishedHandler = m_finishedHandler;
    clearTurn();
    if (finishedHandler)
    {
        finishedHandler(turnId);
    }
}

void AiTurnRunner::clearTurn()
{
    m_retryTimer.stop();
    m_configuration = {};
    m_generation = {};
    m_secretLoader = {};
    m_eventHandler = {};
    m_finishedHandler = {};
    m_retryHandler = {};
    m_jitterSource = {};
    m_requestId.reset();
    m_bufferedStart.reset();
    m_pendingError.reset();
    m_turnId = 0;
    m_completedRetries = 0;
    m_visibleOutputObserved = false;
    m_cancelled = false;
}

} // namespace ztermy::ai
