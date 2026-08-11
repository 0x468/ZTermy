#include "infrastructure/ai/ProviderHttpClient.h"

#include <QByteArray>
#include <QNetworkRequest>
#include <QVariant>

#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maxErrorBodyBytes = qsizetype{64} * 1024;

[[nodiscard]] int statusCode(const QNetworkReply &reply)
{
    return reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

[[nodiscard]] std::optional<std::uint64_t> retryAfter(const QNetworkReply &reply)
{
    const auto header = reply.rawHeader("Retry-After");
    std::uint64_t seconds = 0;
    const auto result = std::from_chars(header.constData(), header.constData() + header.size(), seconds);
    if (result.ec == std::errc{} && result.ptr == header.constData() + header.size())
    {
        return seconds * 1000;
    }
    return std::nullopt;
}

[[nodiscard]] AiProviderError httpError(const QNetworkReply &reply)
{
    const auto status = statusCode(reply);
    auto code = AiProviderErrorCode::server;
    auto retryable = false;
    if (status == 401 || status == 403)
    {
        code = AiProviderErrorCode::authentication;
    }
    else if (status == 429)
    {
        code = AiProviderErrorCode::rateLimited;
        retryable = true;
    }
    else if (status == 408)
    {
        code = AiProviderErrorCode::network;
        retryable = true;
    }
    else if (status >= 400 && status < 500)
    {
        code = AiProviderErrorCode::invalidRequest;
    }
    else if (status >= 500)
    {
        retryable = true;
    }
    return AiProviderError{.code = code,
                           .message = "Provider HTTP request failed with status " + std::to_string(status) + '.',
                           .retryAfterMilliseconds = retryAfter(reply),
                           .retryable = retryable};
}

[[nodiscard]] AiProviderError networkError(const QNetworkReply &reply)
{
    return AiProviderError{.code = AiProviderErrorCode::network,
                           .message = "Provider network request failed.",
                           .retryable = reply.error() != QNetworkReply::SslHandshakeFailedError};
}

} // namespace

ProviderHttpClient::ProviderHttpClient(QNetworkAccessManager *networkAccessManager, QObject *parent)
    : QObject(parent), m_networkAccessManager(networkAccessManager)
{
}

ProviderHttpClient::ProviderHttpClient(QObject *parent)
    : QObject(parent),
      m_ownedNetworkAccessManager(std::make_unique<QNetworkAccessManager>()),
      m_networkAccessManager(m_ownedNetworkAccessManager.get())
{
}

ProviderHttpClient::~ProviderHttpClient()
{
    for (const auto &[reply, state] : m_requests)
    {
        static_cast<void>(state);
        reply->disconnect(this);
        reply->abort();
    }
}

std::expected<ProviderHttpClient::RequestId, AiProviderError>
ProviderHttpClient::start(const AiProviderConfiguration &configuration, const AiGenerationRequest &generation,
                          security::SensitiveByteArray apiKey, EventHandler eventHandler,
                          FinishedHandler finishedHandler)
{
    if (m_networkAccessManager == nullptr)
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::network,
                                               .message = "Provider network service is unavailable.",
                                               .retryable = false});
    }
    const auto prepared = ProviderRequestFactory::prepare(configuration, generation, apiKey.view());
    apiKey.clear();
    if (!prepared.has_value())
    {
        return std::unexpected(prepared.error());
    }

    auto state = std::make_unique<RequestState>();
    state->id = m_nextRequestId++;
    state->provider = configuration.kind;
    state->protocol = prepared->protocol;
    state->eventHandler = std::move(eventHandler);
    state->finishedHandler = std::move(finishedHandler);
    const auto requestId = state->id;
    auto *reply = m_networkAccessManager->post(prepared->request, prepared->body);
    attach(reply, std::move(state));
    return requestId;
}

bool ProviderHttpClient::cancel(const RequestId requestId)
{
    const auto iterator = std::ranges::find_if(m_requests, [requestId](const auto &entry) {
        return entry.second->id == requestId;
    });
    if (iterator == m_requests.end())
    {
        return false;
    }
    auto &state = *iterator->second;
    fail(state,
         AiProviderError{.code = AiProviderErrorCode::cancelled,
                         .message = "Provider request was cancelled.",
                         .retryable = false},
         iterator->first);
    return true;
}

void ProviderHttpClient::attach(QNetworkReply *reply, std::unique_ptr<RequestState> state)
{
    m_requests.emplace(reply, std::move(state));
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        consumeAvailable(reply);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        finish(reply);
    });
}

void ProviderHttpClient::consumeAvailable(QNetworkReply *reply)
{
    const auto iterator = m_requests.find(reply);
    if (iterator == m_requests.end() || iterator->second->failed)
    {
        reply->readAll();
        return;
    }
    auto &state = *iterator->second;
    const auto bytes = reply->readAll();
    if (statusCode(*reply) >= 400)
    {
        const auto available = maxErrorBodyBytes - std::min(state.errorBody.size(), maxErrorBodyBytes);
        state.errorBody.append(bytes.left(available));
        return;
    }
    const auto view = std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    if (state.protocol == AiWireProtocol::serverSentEvents)
    {
        consumeSse(state, view, reply);
    }
    else
    {
        consumeNdjson(state, view, reply);
    }
}

void ProviderHttpClient::consumeSse(RequestState &state, const std::string_view bytes, QNetworkReply *reply)
{
    const auto parsed = state.sseParser.append(bytes);
    if (!parsed.has_value())
    {
        fail(state,
             AiProviderError{.code = AiProviderErrorCode::protocol,
                             .message = "Provider SSE stream exceeded its parser limits.",
                             .retryable = false},
             reply);
        return;
    }
    for (const auto &event : parsed.value())
    {
        const auto mapped = state.provider == AiProviderKind::openAiResponses ? state.openAiMapper.map(event)
                                                                              : state.compatibleMapper.map(event);
        if (!mapped.has_value())
        {
            fail(state, mapped.error(), reply);
            return;
        }
        dispatch(state, mapped.value());
    }
}

void ProviderHttpClient::consumeNdjson(RequestState &state, const std::string_view bytes, QNetworkReply *reply)
{
    const auto parsed = state.ndjsonParser.append(bytes);
    if (!parsed.has_value())
    {
        fail(state,
             AiProviderError{.code = AiProviderErrorCode::protocol,
                             .message = "Provider NDJSON stream exceeded its parser limits.",
                             .retryable = false},
             reply);
        return;
    }
    for (const auto &line : parsed.value())
    {
        const auto mapped = state.ollamaMapper.map(line);
        if (!mapped.has_value())
        {
            fail(state, mapped.error(), reply);
            return;
        }
        dispatch(state, mapped.value());
    }
}

void ProviderHttpClient::dispatch(RequestState &state, const std::vector<AiStreamEvent> &events)
{
    for (const auto &event : events)
    {
        if (event.type == AiStreamEventType::responseCompleted || event.type == AiStreamEventType::responseFailed)
        {
            state.terminalEventSeen = true;
        }
        if (state.eventHandler)
        {
            state.eventHandler(state.id, event);
        }
    }
}

void ProviderHttpClient::fail(RequestState &state, AiProviderError error, QNetworkReply *reply)
{
    if (state.failed)
    {
        return;
    }
    state.failed = true;
    state.terminalEventSeen = true;
    dispatch(state, {AiStreamEvent{.type = AiStreamEventType::responseFailed, .error = std::move(error)}});
    reply->abort();
}

void ProviderHttpClient::finish(QNetworkReply *reply)
{
    const auto iterator = m_requests.find(reply);
    if (iterator == m_requests.end())
    {
        return;
    }
    consumeAvailable(reply);
    auto &state = *iterator->second;
    if (!state.failed && statusCode(*reply) >= 400)
    {
        fail(state, httpError(*reply), reply);
    }
    else if (!state.failed && reply->error() != QNetworkReply::NoError)
    {
        fail(state, networkError(*reply), reply);
    }
    else if (!state.failed)
    {
        if (state.protocol == AiWireProtocol::serverSentEvents)
        {
            const auto parsed = state.sseParser.finish();
            if (parsed.has_value())
            {
                for (const auto &event : parsed.value())
                {
                    const auto mapped = state.provider == AiProviderKind::openAiResponses
                                            ? state.openAiMapper.map(event)
                                            : state.compatibleMapper.map(event);
                    if (mapped.has_value())
                    {
                        dispatch(state, mapped.value());
                    }
                    else
                    {
                        fail(state, mapped.error(), reply);
                        break;
                    }
                }
            }
        }
        else
        {
            const auto parsed = state.ndjsonParser.finish();
            if (parsed.has_value())
            {
                for (const auto &line : parsed.value())
                {
                    const auto mapped = state.ollamaMapper.map(line);
                    if (mapped.has_value())
                    {
                        dispatch(state, mapped.value());
                    }
                    else
                    {
                        fail(state, mapped.error(), reply);
                        break;
                    }
                }
            }
        }
        if (!state.failed && !state.terminalEventSeen)
        {
            fail(state,
                 AiProviderError{.code = AiProviderErrorCode::protocol,
                                 .message = "Provider stream ended before a completion event.",
                                 .retryable = true},
                 reply);
        }
    }
    release(reply);
}

void ProviderHttpClient::release(QNetworkReply *reply)
{
    const auto iterator = m_requests.find(reply);
    if (iterator == m_requests.end())
    {
        return;
    }
    auto state = std::move(iterator->second);
    m_requests.erase(iterator);
    if (state->finishedHandler)
    {
        state->finishedHandler(state->id);
    }
    reply->deleteLater();
}

} // namespace ztermy::ai
