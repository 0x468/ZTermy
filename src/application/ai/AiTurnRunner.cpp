#include "application/ai/AiTurnRunner.h"

#include <QPointer>
#include <QRandomGenerator>

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace ztermy::ai
{

AiTurnRunner::AiTurnRunner(ProviderHttpClient &client, AiProviderRetryPolicy retryPolicy, QObject *parent)
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
AiTurnRunner::start(AiProviderConfiguration configuration, AiGenerationRequest generation, SecretLoader secretLoader,
                    EventHandler eventHandler, FinishedHandler finishedHandler, RetryHandler retryHandler,
                    JitterSource jitterSource, ToolHandler toolHandler)
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
    m_toolHandler = std::move(toolHandler);
    m_turnId = m_nextTurnId++;
    m_completedRetries = 0;
    m_completedToolCalls = 0;
    m_startedAt = std::chrono::steady_clock::now();
    m_firstTokenAt.reset();
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
        m_configuration, m_generation, std::move(secret.value()),
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

void AiTurnRunner::handleEvent(const ProviderHttpClient::RequestId requestId, const AiStreamEvent &event)
{
    if (!m_requestId.has_value() || *m_requestId != requestId || !active())
    {
        return;
    }
    if (event.type == AiStreamEventType::responseStarted)
    {
        if (!event.responseId.empty())
        {
            m_responseId = event.responseId;
        }
        m_bufferedStart = event;
        return;
    }

    if (event.type == AiStreamEventType::toolCallStarted || event.type == AiStreamEventType::toolArgumentsDelta
        || event.type == AiStreamEventType::toolCallCompleted)
    {
        observeToolEvent(event);
    }
    if (event.type == AiStreamEventType::responseCompleted && !m_pendingToolCalls.empty())
    {
        if (!event.responseId.empty())
        {
            m_responseId = event.responseId;
        }
        m_toolContinuationPending = true;
        return;
    }
    if (event.type == AiStreamEventType::responseFailed)
    {
        m_pendingError = event.error.value_or(AiProviderError{.code = AiProviderErrorCode::protocol,
                                                              .message = "Provider failed without an error payload.",
                                                              .retryable = false});
        return;
    }

    emitBufferedStart();
    if (event.type == AiStreamEventType::textDelta && !event.delta.empty() && !m_firstTokenAt.has_value())
    {
        m_firstTokenAt = std::chrono::steady_clock::now();
    }
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
        if (m_toolContinuationPending)
        {
            const auto continued = continueWithTools();
            if (!continued.has_value())
            {
                finishWithError(continued.error());
            }
            return;
        }
        finishTurn();
        return;
    }

    const auto error = *m_pendingError;
    const auto jitter = m_jitterSource ? m_jitterSource() : QRandomGenerator::global()->generateDouble();
    const auto decision = m_retryPolicy.decide(error, m_completedRetries, jitter);
    if (!m_cancelled && !m_visibleOutputObserved && decision.retry)
    {
        ++m_completedRetries;
        m_bufferedStart.reset();
        m_pendingError.reset();
        m_retryTimer.start(static_cast<int>(std::min<std::uint64_t>(
            decision.delayMilliseconds, static_cast<std::uint64_t>(std::numeric_limits<int>::max()))));
        if (m_retryHandler)
        {
            m_retryHandler(m_turnId, m_completedRetries, decision.delayMilliseconds);
        }
        return;
    }
    finishWithError(error);
}

std::expected<void, AiProviderError> AiTurnRunner::continueWithTools()
{
    constexpr std::uint32_t maximumToolCalls = 24;
    if (!m_toolHandler)
    {
        return std::unexpected(
            AiProviderError{.code = AiProviderErrorCode::protocol,
                            .message = "The provider requested a tool but no tool handler is available.",
                            .retryable = false});
    }
    if (m_pendingToolCalls.empty() || m_pendingToolCalls.size() > maximumToolCalls - m_completedToolCalls)
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::protocol,
                                               .message = "The AI turn exceeded its 24 tool-call budget.",
                                               .retryable = false});
    }
    if (m_configuration.kind == AiProviderKind::openAiResponses && m_responseId.empty())
    {
        return std::unexpected(
            AiProviderError{.code = AiProviderErrorCode::protocol,
                            .message = "The provider omitted the response id required for tool continuation.",
                            .retryable = false});
    }

    AiToolExchange exchange{.calls = m_pendingToolCalls};
    exchange.outputs.reserve(exchange.calls.size());
    for (const auto &call : exchange.calls)
    {
        if (call.id.empty() || call.name.empty() || call.argumentsJson.empty())
        {
            return std::unexpected(AiProviderError{.code = AiProviderErrorCode::protocol,
                                                   .message = "The provider emitted an incomplete tool call.",
                                                   .retryable = false});
        }
        auto output = m_toolHandler(call);
        if (!output.has_value())
        {
            return std::unexpected(output.error());
        }
        exchange.outputs.push_back(std::move(*output));
    }
    m_completedToolCalls += static_cast<std::uint32_t>(exchange.calls.size());
    m_generation.toolHistory.push_back(std::move(exchange));
    if (!m_responseId.empty())
    {
        m_generation.previousResponseId = m_responseId;
    }
    m_pendingToolCalls.clear();
    m_responseId.clear();
    m_toolContinuationPending = false;
    return startAttempt();
}

void AiTurnRunner::observeToolEvent(const AiStreamEvent &event)
{
    constexpr std::size_t maximumArgumentsBytes = std::size_t{16} * 1024;
    auto tool = std::ranges::find(m_pendingToolCalls, event.toolCallId, &AiToolCall::id);
    if (tool == m_pendingToolCalls.end())
    {
        if (event.toolCallId.empty())
        {
            return;
        }
        m_pendingToolCalls.push_back(AiToolCall{.id = event.toolCallId, .name = event.toolName});
        tool = std::prev(m_pendingToolCalls.end());
    }
    if (!event.toolName.empty())
    {
        tool->name = event.toolName;
    }
    if (event.type == AiStreamEventType::toolCallCompleted && !event.delta.empty())
    {
        tool->argumentsJson = event.delta;
    }
    else if (event.type == AiStreamEventType::toolArgumentsDelta && !event.delta.empty())
    {
        if (event.delta.size() > maximumArgumentsBytes - std::min(maximumArgumentsBytes, tool->argumentsJson.size()))
        {
            m_pendingError = AiProviderError{.code = AiProviderErrorCode::protocol,
                                             .message = "Tool arguments exceed the 16 KiB limit.",
                                             .retryable = false};
            static_cast<void>(m_client.cancel(*m_requestId));
            return;
        }
        tool->argumentsJson += event.delta;
    }
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
    m_eventHandler(m_turnId, AiStreamEvent{.type = AiStreamEventType::responseFailed, .error = std::move(error)});
    finishTurn();
}

void AiTurnRunner::finishTurn()
{
    const auto turnId = m_turnId;
    const auto finishedAt = std::chrono::steady_clock::now();
    const auto elapsedMilliseconds = [](const auto start, const auto finish) {
        return static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count()));
    };
    const AiTurnMetrics metrics{.wallTimeMilliseconds = elapsedMilliseconds(m_startedAt, finishedAt),
                                .firstTokenMilliseconds =
                                    m_firstTokenAt.has_value()
                                        ? std::optional{elapsedMilliseconds(m_startedAt, *m_firstTokenAt)}
                                        : std::nullopt,
                                .retryCount = m_completedRetries};
    auto finishedHandler = m_finishedHandler;
    clearTurn();
    if (finishedHandler)
    {
        finishedHandler(turnId, metrics);
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
    m_toolHandler = {};
    m_requestId.reset();
    m_bufferedStart.reset();
    m_pendingError.reset();
    m_pendingToolCalls.clear();
    m_responseId.clear();
    m_turnId = 0;
    m_completedRetries = 0;
    m_completedToolCalls = 0;
    m_startedAt = {};
    m_firstTokenAt.reset();
    m_visibleOutputObserved = false;
    m_toolContinuationPending = false;
    m_cancelled = false;
}

} // namespace ztermy::ai
