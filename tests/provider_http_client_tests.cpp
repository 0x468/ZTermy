#include "core/security/SensitiveByteArray.h"
#include "infrastructure/ai/ProviderHttpClient.h"

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace
{

using ztermy::ai::AiGenerationRequest;
using ztermy::ai::AiProviderConfiguration;
using ztermy::ai::AiProviderErrorCode;
using ztermy::ai::AiProviderKind;
using ztermy::ai::AiStreamEvent;
using ztermy::ai::AiStreamEventType;
using ztermy::ai::ProviderHttpClient;
using ztermy::security::SensitiveByteArray;

struct FakeResponse final
{
    int status = 200;
    QByteArray payload;
    QByteArray retryAfter;
    QByteArray requestId;
    int delayMilliseconds = 0;
};

class FakeReply final : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, FakeResponse response, QObject *parent)
        : QNetworkReply(parent), m_response(std::move(response))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_response.status);
        if (!m_response.retryAfter.isEmpty())
        {
            setRawHeader("Retry-After", m_response.retryAfter);
        }
        if (!m_response.requestId.isEmpty())
        {
            setRawHeader("x-request-id", m_response.requestId);
        }
        open(QIODevice::ReadOnly);
        QTimer::singleShot(m_response.delayMilliseconds, this, [this] {
            deliver();
        });
    }

    void abort() override
    {
        if (m_finished)
        {
            return;
        }
        m_aborted = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        QTimer::singleShot(0, this, [this] {
            complete();
        });
    }

    [[nodiscard]] qint64 bytesAvailable() const override
    {
        return static_cast<qint64>(m_buffer.size()) + QNetworkReply::bytesAvailable();
    }

    [[nodiscard]] bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, const qint64 maximumSize) override
    {
        if (m_buffer.isEmpty())
        {
            return m_finished ? -1 : 0;
        }
        const auto count = std::min(maximumSize, static_cast<qint64>(m_buffer.size()));
        std::memcpy(data, m_buffer.constData(), static_cast<std::size_t>(count));
        m_buffer.remove(0, count);
        return count;
    }

private:
    void deliver()
    {
        if (m_aborted || m_finished)
        {
            return;
        }
        m_buffer = m_response.payload;
        emit readyRead();
        complete();
    }

    void complete()
    {
        if (m_finished)
        {
            return;
        }
        m_finished = true;
        setFinished(true);
        emit finished();
    }

    FakeResponse m_response;
    QByteArray m_buffer;
    bool m_aborted = false;
    bool m_finished = false;
};

class FakeNetworkAccessManager final : public QNetworkAccessManager
{
public:
    void enqueue(FakeResponse response) { m_responses.push_back(std::move(response)); }

protected:
    QNetworkReply *createRequest(const Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        static_cast<void>(operation);
        static_cast<void>(outgoingData);
        auto response = std::move(m_responses.front());
        m_responses.erase(m_responses.begin());
        return new FakeReply(request, std::move(response), this);
    }

private:
    std::vector<FakeResponse> m_responses;
};

[[nodiscard]] AiProviderConfiguration openAiConfiguration()
{
    return AiProviderConfiguration{.kind = AiProviderKind::openAiResponses,
                                   .baseUrl = "https://api.openai.test/v1",
                                   .model = "model"};
}

class ProviderHttpClientTests final : public QObject
{
    Q_OBJECT

private slots:
    void streamsTypedEventsAndFinishes();
    void streamsAnthropicTypedEvents();
    void streamsCompatibleTypedEvents();
    void streamsOllamaTypedEvents();
    void classifiesRateLimitAndRetryDelay();
    void classifiesSpecificAuthenticationCode();
    void classifiesQuotaFromProviderBody();
    void classifiesContextOverflowAsRetryable();
    void cancelsInFlightRequest();
    void tracesExactPayloadWithoutAuthorizationSecret();
};

void ProviderHttpClientTests::streamsTypedEventsAndFinishes()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{.payload =
                                     "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n\n"
                                     "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\n\n"
                                     "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\"}}\n\n"});
    ProviderHttpClient client(&network);
    std::vector<AiStreamEvent> events;
    bool finished = false;
    const auto requestId = client.start(
        openAiConfiguration(), AiGenerationRequest{}, SensitiveByteArray{},
        [&events](const auto, const AiStreamEvent &event) {
            events.push_back(event);
        },
        [&finished](const auto) {
            finished = true;
        });
    QVERIFY(requestId.has_value());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(events.size(), std::size_t{3});
    QCOMPARE(events.at(0).type, AiStreamEventType::responseStarted);
    QCOMPARE(events.at(1).type, AiStreamEventType::textDelta);
    QCOMPARE(events.at(2).type, AiStreamEventType::responseCompleted);
}

void ProviderHttpClientTests::streamsCompatibleTypedEvents()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .payload = "data: {\"id\":\"chat_1\",\"choices\":[{\"delta\":{\"content\":\"hi\"},\"finish_reason\":null}]}\n\n"
                   "data: [DONE]\n\n"});
    ProviderHttpClient client(&network);
    const AiProviderConfiguration configuration{.kind = AiProviderKind::openAiCompatible,
                                                .baseUrl = "https://compatible.example/v1",
                                                .model = "model"};
    std::vector<AiStreamEvent> events;
    bool finished = false;
    QVERIFY(client
                .start(
                    configuration, AiGenerationRequest{}, SensitiveByteArray{},
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto) {
                        finished = true;
                    })
                .has_value());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QVERIFY(std::ranges::any_of(events, [](const AiStreamEvent &event) {
        return event.type == AiStreamEventType::textDelta && event.delta == "hi";
    }));
    QCOMPARE(events.back().type, AiStreamEventType::responseCompleted);
}

void ProviderHttpClientTests::streamsAnthropicTypedEvents()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .payload =
            "event: message_start\n"
            "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\",\"usage\":{\"input_tokens\":3}}}\n\n"
            "event: content_block_start\n"
            "data: "
            "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
            "event: content_block_delta\n"
            "data: "
            "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"hi\"}}\n\n"
            "event: content_block_stop\n"
            "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
            "event: message_stop\n"
            "data: {\"type\":\"message_stop\"}\n\n"});
    ProviderHttpClient client(&network);
    const AiProviderConfiguration configuration{.kind = AiProviderKind::anthropicMessages,
                                                .baseUrl = "https://api.anthropic.test",
                                                .model = "claude"};
    std::vector<AiStreamEvent> events;
    bool finished = false;
    QVERIFY(client
                .start(
                    configuration, AiGenerationRequest{}, SensitiveByteArray{},
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto) {
                        finished = true;
                    })
                .has_value());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QVERIFY(std::ranges::any_of(events, [](const AiStreamEvent &event) {
        return event.type == AiStreamEventType::textDelta && event.delta == "hi";
    }));
    QCOMPARE(events.back().type, AiStreamEventType::responseCompleted);
}

void ProviderHttpClientTests::streamsOllamaTypedEvents()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .payload = "{\"message\":{\"content\":\"first\"},\"done\":false}\n"
                   "{\"message\":{\"content\":\"second\"},\"done\":true,\"prompt_eval_count\":3,\"eval_count\":2}\n"});
    ProviderHttpClient client(&network);
    const AiProviderConfiguration configuration{.kind = AiProviderKind::ollama,
                                                .baseUrl = "http://127.0.0.1:11434",
                                                .model = "model"};
    std::vector<AiStreamEvent> events;
    bool finished = false;
    QVERIFY(client
                .start(
                    configuration, AiGenerationRequest{}, SensitiveByteArray{},
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto) {
                        finished = true;
                    })
                .has_value());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QVERIFY(std::ranges::any_of(events, [](const AiStreamEvent &event) {
        return event.type == AiStreamEventType::textDelta && event.delta == "first";
    }));
    QVERIFY(std::ranges::any_of(events, [](const AiStreamEvent &event) {
        return event.type == AiStreamEventType::usageUpdated && event.usage.has_value()
               && event.usage->outputTokens == 2;
    }));
    QCOMPARE(events.back().type, AiStreamEventType::responseCompleted);
}

void ProviderHttpClientTests::classifiesRateLimitAndRetryDelay()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .status = 429,
        .payload =
            R"json({"error":{"message":"Rate limit reached for this model.","type":"rate_limit_error","code":"rate_limit_exceeded"}})json",
        .retryAfter = "3",
        .requestId = "req_rate_1"});
    ProviderHttpClient client(&network);
    std::vector<AiStreamEvent> events;
    bool finished = false;
    QVERIFY(client
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, SensitiveByteArray{},
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto) {
                        finished = true;
                    })
                .has_value());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(events.size(), std::size_t{1});
    QVERIFY(events.front().error.has_value());
    const auto error = events.front().error.value_or(ztermy::ai::AiProviderError{});
    QCOMPARE(error.code, AiProviderErrorCode::rateLimited);
    QCOMPARE(error.message, std::string("Rate limit reached for this model."));
    QCOMPARE(error.retryAfterMilliseconds, std::optional<std::uint64_t>{3000});
    QCOMPARE(error.httpStatus, std::optional<std::uint16_t>{429});
    QCOMPARE(error.providerCode, std::string("rate_limit_exceeded"));
    QCOMPARE(error.requestId, std::string("req_rate_1"));
    QVERIFY(error.retryable);
}

void ProviderHttpClientTests::classifiesSpecificAuthenticationCode()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .status = 401,
        .payload =
            R"json({"error":{"message":"The API key is invalid.","type":"invalid_request_error","code":"invalid_api_key"}})json"});
    ProviderHttpClient client(&network);
    std::vector<AiStreamEvent> events;
    bool finished = false;
    QVERIFY(client
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, SensitiveByteArray{},
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto) {
                        finished = true;
                    })
                .has_value());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(events.size(), std::size_t{1});
    const auto error = events.front().error.value_or(ztermy::ai::AiProviderError{});
    QCOMPARE(error.code, AiProviderErrorCode::authentication);
    QCOMPARE(error.message, std::string("The API key is invalid."));
    QCOMPARE(error.httpStatus, std::optional<std::uint16_t>{401});
    QCOMPARE(error.providerCode, std::string("invalid_api_key"));
    QVERIFY(!error.retryable);
}

void ProviderHttpClientTests::classifiesQuotaFromProviderBody()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .status = 402,
        .payload =
            R"json({"error":{"message":"Insufficient credits.","code":"insufficient_quota"},"request_id":"req_quota_1"})json"});
    ProviderHttpClient client(&network);
    std::vector<AiStreamEvent> events;
    bool finished = false;
    QVERIFY(client
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, SensitiveByteArray{},
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto) {
                        finished = true;
                    })
                .has_value());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(events.size(), std::size_t{1});
    const auto error = events.front().error.value_or(ztermy::ai::AiProviderError{});
    QCOMPARE(error.code, AiProviderErrorCode::quotaExceeded);
    QCOMPARE(error.message, std::string("Insufficient credits."));
    QCOMPARE(error.httpStatus, std::optional<std::uint16_t>{402});
    QCOMPARE(error.providerCode, std::string("insufficient_quota"));
    QCOMPARE(error.requestId, std::string("req_quota_1"));
    QVERIFY(!error.retryable);
}

void ProviderHttpClientTests::classifiesContextOverflowAsRetryable()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{.status = 413, .payload = "request entity too large"});
    ProviderHttpClient client(&network);
    std::vector<AiStreamEvent> events;
    bool finished = false;
    QVERIFY(client
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, SensitiveByteArray{},
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto) {
                        finished = true;
                    })
                .has_value());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(events.size(), std::size_t{1});
    QVERIFY(events.front().error.has_value());
    const auto error = events.front().error.value_or(ztermy::ai::AiProviderError{});
    QCOMPARE(error.code, AiProviderErrorCode::contextOverflow);
    QCOMPARE(error.message, std::string("request entity too large"));
    QCOMPARE(error.httpStatus, std::optional<std::uint16_t>{413});
    QVERIFY(error.retryable);
}

void ProviderHttpClientTests::cancelsInFlightRequest()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{.payload = "data: ignored\n\n", .delayMilliseconds = 500});
    ProviderHttpClient client(&network);
    std::vector<AiStreamEvent> events;
    bool finished = false;
    const auto requestId = client.start(
        openAiConfiguration(), AiGenerationRequest{}, SensitiveByteArray{},
        [&events](const auto, const AiStreamEvent &event) {
            events.push_back(event);
        },
        [&finished](const auto) {
            finished = true;
        });
    QVERIFY(requestId.has_value());
    QVERIFY(client.cancel(requestId.value()));
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(events.size(), std::size_t{1});
    const auto error = events.front().error.value_or(ztermy::ai::AiProviderError{});
    QCOMPARE(error.code, AiProviderErrorCode::cancelled);
    QVERIFY(!client.cancel(requestId.value()));
}

void ProviderHttpClientTests::tracesExactPayloadWithoutAuthorizationSecret()
{
    FakeNetworkAccessManager network;
    network.enqueue(
        FakeResponse{.payload = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello\"}\n\n"
                                "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_trace\"}}\n\n"});
    ProviderHttpClient client(&network);
    std::vector<std::pair<QString, QByteArray>> trace;
    client.setTraceHandler([&trace](const auto, QString event, QByteArray payload) {
        trace.emplace_back(std::move(event), std::move(payload));
    });
    AiGenerationRequest request;
    request.messages.push_back({.role = ztermy::ai::AiMessageRole::user, .content = "trace this exact prompt"});
    bool finished = false;
    QVERIFY(client
                .start(
                    openAiConfiguration(), request, SensitiveByteArray{QByteArray("secret-token")},
                    [](const auto, const AiStreamEvent &) {
                    },
                    [&finished](const auto) {
                        finished = true;
                    })
                .has_value());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(trace.size(), std::size_t{2});
    QCOMPARE(trace.front().first, QStringLiteral("provider_request"));
    QVERIFY(trace.front().second.contains("trace this exact prompt"));
    QVERIFY(!trace.front().second.contains("secret-token"));
    QCOMPARE(trace.back().first, QStringLiteral("provider_response_bytes"));
    QVERIFY(trace.back().second.contains("hello"));
}

} // namespace

QTEST_GUILESS_MAIN(ProviderHttpClientTests)

#include "provider_http_client_tests.moc"
