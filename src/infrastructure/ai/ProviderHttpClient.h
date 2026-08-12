#pragma once

#include "core/security/SensitiveByteArray.h"
#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/NdjsonParser.h"
#include "domain/ai/ServerSentEventParser.h"
#include "infrastructure/ai/ProviderRequestFactory.h"
#include "infrastructure/ai/ProviderStreamMapper.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <unordered_map>

namespace ztermy::ai
{

class ProviderHttpClient final : public QObject
{
public:
    using RequestId = std::uint64_t;
    using EventHandler = std::function<void(RequestId, const AiStreamEvent &)>;
    using FinishedHandler = std::function<void(RequestId)>;
    using TraceHandler = std::function<void(RequestId, const QString &, const QByteArray &)>;

    explicit ProviderHttpClient(QNetworkAccessManager *networkAccessManager, QObject *parent = nullptr);
    explicit ProviderHttpClient(QObject *parent = nullptr);
    ~ProviderHttpClient() override;

    ProviderHttpClient(const ProviderHttpClient &) = delete;
    ProviderHttpClient &operator=(const ProviderHttpClient &) = delete;

    [[nodiscard]] std::expected<RequestId, AiProviderError>
    start(const AiProviderConfiguration &configuration, const AiGenerationRequest &generation,
          security::SensitiveByteArray apiKey, EventHandler eventHandler, FinishedHandler finishedHandler);
    [[nodiscard]] bool cancel(RequestId requestId);
    void setTraceHandler(TraceHandler handler);

private:
    struct RequestState final
    {
        RequestId id = 0;
        AiProviderKind provider = AiProviderKind::openAiResponses;
        AiWireProtocol protocol = AiWireProtocol::serverSentEvents;
        ServerSentEventParser sseParser;
        NdjsonParser ndjsonParser;
        OpenAiResponsesStreamMapper openAiMapper;
        OpenAiCompatibleStreamMapper compatibleMapper;
        AnthropicStreamMapper anthropicMapper;
        OllamaStreamMapper ollamaMapper;
        EventHandler eventHandler;
        FinishedHandler finishedHandler;
        QByteArray errorBody;
        bool failed = false;
        bool terminalEventSeen = false;
    };

    void attach(QNetworkReply *reply, std::unique_ptr<RequestState> state);
    void consumeAvailable(QNetworkReply *reply);
    void consumeSse(RequestState &state, std::string_view bytes, QNetworkReply *reply);
    void consumeNdjson(RequestState &state, std::string_view bytes, QNetworkReply *reply);
    void dispatch(RequestState &state, const std::vector<AiStreamEvent> &events);
    void fail(RequestState &state, AiProviderError error, QNetworkReply *reply);
    void finish(QNetworkReply *reply);
    void release(QNetworkReply *reply);

    std::unique_ptr<QNetworkAccessManager> m_ownedNetworkAccessManager;
    QNetworkAccessManager *m_networkAccessManager = nullptr;
    std::unordered_map<QNetworkReply *, std::unique_ptr<RequestState>> m_requests;
    RequestId m_nextRequestId = 1;
    TraceHandler m_traceHandler;
};

} // namespace ztermy::ai
