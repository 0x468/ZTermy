#include "application/ai/CodexAgentTurnRunner.h"

#include <QJsonObject>

#include <algorithm>
#include <chrono>
#include <utility>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] AiProviderError protocolError(const QString &message)
{
    const QByteArray encoded = message.toUtf8();
    return {.code = AiProviderErrorCode::protocol,
            .message = std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())),
            .retryable = false};
}

[[nodiscard]] std::uint64_t elapsedMilliseconds(const std::chrono::steady_clock::time_point start,
                                                const std::chrono::steady_clock::time_point end)
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed));
}

} // namespace

CodexAgentTurnRunner::CodexAgentTurnRunner(QObject *parent) : QObject(parent) {}

CodexAgentTurnRunner::~CodexAgentTurnRunner()
{
    stop();
}

std::expected<void, AiProviderError> CodexAgentTurnRunner::initialize(CodexAppServerConfiguration configuration,
                                                                      ReadyHandler readyHandler)
{
    if (active())
    {
        return std::unexpected(protocolError(QStringLiteral("An active Codex turn cannot be reinitialized.")));
    }
    stop();
    m_readyHandler = std::move(readyHandler);
    auto started = m_client.start(
        std::move(configuration),
        [this](auto result) {
            if (result.has_value())
            {
                m_initialized = true;
            }
            if (m_readyHandler)
            {
                auto handler = std::move(m_readyHandler);
                if (result.has_value())
                {
                    handler(std::move(*result));
                }
                else
                {
                    handler(std::unexpected(protocolError(result.error())));
                }
            }
        },
        [this](auto message) {
            handleClientEvent(std::move(message));
        },
        [this](const CodexDynamicToolCall &call) {
            handleToolCall(call);
        });
    if (!started.has_value())
    {
        m_readyHandler = {};
        return std::unexpected(protocolError(started.error()));
    }
    return {};
}

std::expected<CodexAgentTurnRunner::TurnId, AiProviderError>
CodexAgentTurnRunner::start(const std::string_view prompt, EventHandler eventHandler, FinishedHandler finishedHandler,
                            ToolHandler toolHandler, ToolOutputHandler toolOutputHandler)
{
    if (!ready() || active() || prompt.empty())
    {
        return std::unexpected(protocolError(QStringLiteral("The Codex Agent is not ready for a new turn.")));
    }
    m_mapper.reset();
    m_eventHandler = std::move(eventHandler);
    m_finishedHandler = std::move(finishedHandler);
    m_toolHandler = std::move(toolHandler);
    m_toolOutputHandler = std::move(toolOutputHandler);
    m_pendingTool.reset();
    m_startedAt = std::chrono::steady_clock::now();
    m_firstTokenAt.reset();
    m_turnId = m_nextTurnId++;
    const auto started = m_client.startTurn(prompt);
    if (!started.has_value())
    {
        const auto error = protocolError(started.error());
        clearTurn();
        return std::unexpected(error);
    }
    return m_turnId;
}

bool CodexAgentTurnRunner::cancel()
{
    if (!active())
    {
        return false;
    }
    if (m_pendingTool.has_value())
    {
        if (m_pendingTool->cancel)
        {
            m_pendingTool->cancel();
        }
        const auto toolCancelled =
            m_client.completeToolCall(m_pendingTool->requestId, false, R"({"ok":false,"error":"cancelled"})");
        m_pendingTool.reset();
        if (!toolCancelled.has_value())
        {
            finishWithError(protocolError(toolCancelled.error()));
            return true;
        }
    }
    const auto interrupted = m_client.interrupt();
    if (!interrupted.has_value())
    {
        finishWithError(protocolError(interrupted.error()));
    }
    return true;
}

bool CodexAgentTurnRunner::completePendingTool(const AiToolOutput &output)
{
    if (!active() || !m_pendingTool.has_value() || output.callId != m_pendingTool->call.id
        || output.name != m_pendingTool->call.name)
    {
        return false;
    }
    const auto pending = std::move(*m_pendingTool);
    m_pendingTool.reset();
    const auto completed = m_client.completeToolCall(pending.requestId, true, output.outputJson);
    if (!completed.has_value())
    {
        finishWithError(protocolError(completed.error()));
        return false;
    }
    if (m_toolOutputHandler)
    {
        m_toolOutputHandler(pending.call, output);
    }
    return true;
}

std::optional<AiToolCall> CodexAgentTurnRunner::pendingToolCall() const
{
    return m_pendingTool.has_value() ? std::optional<AiToolCall>{m_pendingTool->call} : std::nullopt;
}

bool CodexAgentTurnRunner::ready() const noexcept
{
    return m_initialized && m_client.ready();
}

bool CodexAgentTurnRunner::active() const noexcept
{
    return m_turnId != 0;
}

CodexAgentTurnRunner::TurnId CodexAgentTurnRunner::activeTurnId() const noexcept
{
    return m_turnId;
}

QString CodexAgentTurnRunner::threadId() const
{
    return m_client.threadId();
}

void CodexAgentTurnRunner::stop()
{
    if (m_pendingTool.has_value() && m_pendingTool->cancel)
    {
        m_pendingTool->cancel();
    }
    m_pendingTool.reset();
    m_client.stop();
    m_mapper.reset();
    m_readyHandler = {};
    m_initialized = false;
    clearTurn();
}

void CodexAgentTurnRunner::handleClientEvent(std::expected<CodexAppServerMessage, QString> message)
{
    if (!message.has_value())
    {
        if (active())
        {
            finishWithError(protocolError(message.error()));
        }
        return;
    }
    if (message->kind == CodexAppServerMessageKind::response && !message->error.isEmpty())
    {
        finishWithError(protocolError(message->error.value(QStringLiteral("message"))
                                          .toString(QStringLiteral("The Codex turn request failed."))));
        return;
    }
    auto mapped = m_mapper.map(*message);
    if (!mapped.has_value())
    {
        m_client.stop();
        finishWithError(protocolError(mapped.error()));
        return;
    }
    for (const AiStreamEvent &event : *mapped)
    {
        if (!active())
        {
            return;
        }
        if (!m_firstTokenAt.has_value()
            && (event.type == AiStreamEventType::textDelta || event.type == AiStreamEventType::reasoningDelta))
        {
            m_firstTokenAt = std::chrono::steady_clock::now();
        }
        if (m_eventHandler)
        {
            m_eventHandler(m_turnId, event);
        }
        if (event.type == AiStreamEventType::responseCompleted || event.type == AiStreamEventType::responseFailed)
        {
            finishTurn();
            return;
        }
    }
}

void CodexAgentTurnRunner::handleToolCall(const CodexDynamicToolCall &call)
{
    if (!active() || m_pendingTool.has_value())
    {
        const auto completed =
            m_client.completeToolCall(call.requestId, false, R"({"ok":false,"error":"another_tool_call_is_pending"})");
        if (!completed.has_value())
        {
            finishWithError(protocolError(completed.error()));
        }
        return;
    }
    const QByteArray tool = call.tool.toUtf8();
    const QByteArray callId = call.callId.toUtf8();
    const AiToolCall toolCall{.id = std::string(callId.constData(), static_cast<std::size_t>(callId.size())),
                              .name = std::string(tool.constData(), static_cast<std::size_t>(tool.size())),
                              .argumentsJson = call.argumentsJson};
    if (!m_toolHandler)
    {
        const auto completed =
            m_client.completeToolCall(call.requestId, false, R"({"ok":false,"error":"dispatcher_unavailable"})");
        if (!completed.has_value())
        {
            finishWithError(protocolError(completed.error()));
        }
        return;
    }
    auto handled = m_toolHandler(toolCall);
    if (!handled.has_value())
    {
        const auto completed = m_client.completeToolCall(call.requestId, false, handled.error().message);
        if (!completed.has_value())
        {
            finishWithError(protocolError(completed.error()));
        }
        return;
    }
    if (handled->output.has_value())
    {
        const AiToolOutput output = std::move(*handled->output);
        const auto completed = m_client.completeToolCall(call.requestId, true, output.outputJson);
        if (!completed.has_value())
        {
            finishWithError(protocolError(completed.error()));
            return;
        }
        if (m_toolOutputHandler)
        {
            m_toolOutputHandler(toolCall, output);
        }
        return;
    }
    m_pendingTool = PendingTool{.requestId = call.requestId, .call = toolCall, .cancel = std::move(handled->cancel)};
}

void CodexAgentTurnRunner::finishWithError(AiProviderError error)
{
    if (!active())
    {
        return;
    }
    if (m_eventHandler)
    {
        m_eventHandler(m_turnId, AiStreamEvent{.type = AiStreamEventType::responseFailed, .error = std::move(error)});
    }
    finishTurn();
}

void CodexAgentTurnRunner::finishTurn()
{
    const TurnId finishedTurn = m_turnId;
    const AiTurnMetrics finishedMetrics = metrics();
    auto handler = std::move(m_finishedHandler);
    clearTurn();
    if (handler)
    {
        handler(finishedTurn, finishedMetrics);
    }
}

void CodexAgentTurnRunner::clearTurn()
{
    m_eventHandler = {};
    m_finishedHandler = {};
    m_toolHandler = {};
    m_toolOutputHandler = {};
    m_pendingTool.reset();
    m_firstTokenAt.reset();
    m_turnId = 0;
}

AiTurnMetrics CodexAgentTurnRunner::metrics() const
{
    const auto finishedAt = std::chrono::steady_clock::now();
    return {.wallTimeMilliseconds = elapsedMilliseconds(m_startedAt, finishedAt),
            .firstTokenMilliseconds =
                m_firstTokenAt.has_value()
                    ? std::optional<std::uint64_t>{elapsedMilliseconds(m_startedAt, *m_firstTokenAt)}
                    : std::nullopt,
            .retryCount = 0};
}

} // namespace ztermy::ai
