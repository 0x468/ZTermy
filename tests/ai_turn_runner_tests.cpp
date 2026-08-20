#include "application/ai/AiTurnRunner.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
using ztermy::ai::AiProviderError;
using ztermy::ai::AiProviderErrorCode;
using ztermy::ai::AiProviderKind;
using ztermy::ai::AiProviderRetryLimits;
using ztermy::ai::AiProviderRetryPolicy;
using ztermy::ai::AiStreamEvent;
using ztermy::ai::AiStreamEventType;
using ztermy::ai::AiToolOutput;
using ztermy::ai::AiTurnMetrics;
using ztermy::ai::AiTurnRunner;
using ztermy::ai::ProviderHttpClient;
using ztermy::security::SensitiveByteArray;

struct FakeResponse final
{
    int status = 200;
    QByteArray payload;
    QByteArray retryAfter;
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
    [[nodiscard]] std::size_t requestCount() const noexcept { return m_requestCount; }
    [[nodiscard]] const std::vector<qint64> &requestBodySizes() const noexcept { return m_requestBodySizes; }
    [[nodiscard]] const std::vector<QByteArray> &requestBodies() const noexcept { return m_requestBodies; }

protected:
    QNetworkReply *createRequest(const Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        static_cast<void>(operation);
        ++m_requestCount;
        m_requestBodySizes.push_back(outgoingData != nullptr ? outgoingData->size() : 0);
        m_requestBodies.push_back(outgoingData != nullptr ? outgoingData->peek(outgoingData->size()) : QByteArray{});
        if (m_responses.empty())
        {
            return new FakeReply(request, FakeResponse{.status = 500}, this);
        }
        auto response = std::move(m_responses.front());
        m_responses.erase(m_responses.begin());
        return new FakeReply(request, std::move(response), this);
    }

private:
    std::vector<FakeResponse> m_responses;
    std::size_t m_requestCount = 0;
    std::vector<qint64> m_requestBodySizes;
    std::vector<QByteArray> m_requestBodies;
};

[[nodiscard]] AiProviderConfiguration openAiConfiguration()
{
    return AiProviderConfiguration{.kind = AiProviderKind::openAiResponses,
                                   .baseUrl = "https://api.openai.test/v1",
                                   .model = "model"};
}

[[nodiscard]] AiProviderConfiguration anthropicConfiguration()
{
    return AiProviderConfiguration{.kind = AiProviderKind::anthropicMessages,
                                   .baseUrl = "https://api.anthropic.test/v1",
                                   .model = "claude-sonnet-4-6"};
}

[[nodiscard]] auto emptySecretLoader()
{
    return []() -> std::expected<SensitiveByteArray, AiProviderError> {
        return SensitiveByteArray{};
    };
}

[[nodiscard]] AiProviderRetryPolicy fastRetryPolicy()
{
    return AiProviderRetryPolicy(AiProviderRetryLimits{.maxRetries = 2,
                                                       .baseDelayMilliseconds = 1,
                                                       .maxDelayMilliseconds = 2,
                                                       .jitterPercent = 0});
}

class AiTurnRunnerTests final : public QObject
{
    Q_OBJECT

private slots:
    void retriesBeforeVisibleOutput();
    void doesNotReplayAfterVisibleOutput();
    void cancelsScheduledRetry();
    void cancelsActiveRequestSynchronously();
    void destroyingRunnerCancelsSafely();
    void executesReadToolAndContinuesTheSameTurn();
    void waitsForDeferredToolCompletion();
    void cancelsDeferredToolWait();
    void doesNotRetryAfterSideEffectingTool();
    void rejectsOversizedCompletedToolArguments();
    void compactsAndRetriesOnContextOverflow();
    void continuesAnthropicPausedTurnWithOpaqueContent();
    void boundsAnthropicPauseContinuations();
};

void AiTurnRunnerTests::retriesBeforeVisibleOutput()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{.status = 500, .payload = "temporary"});
    network.enqueue(FakeResponse{.payload =
                                     "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n\n"
                                     "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\n\n"
                                     "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\"}}\n\n"});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    std::uint32_t retryCount = 0;
    std::optional<AiTurnMetrics> metrics;
    bool finished = false;

    QVERIFY(runner
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished, &metrics](const auto, const AiTurnMetrics &value) {
                        metrics = value;
                        finished = true;
                    },
                    [&retryCount](const auto, const auto attempt, const auto) {
                        retryCount = attempt;
                    },
                    [] {
                        return 0.5;
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(network.requestCount(), std::size_t{2});
    QCOMPARE(retryCount, std::uint32_t{1});
    QVERIFY(metrics.has_value());
    const auto observedMetrics = metrics.value_or(AiTurnMetrics{});
    QCOMPARE(observedMetrics.retryCount, std::uint32_t{1});
    QVERIFY(observedMetrics.firstTokenMilliseconds.has_value());
    QVERIFY(observedMetrics.firstTokenMilliseconds.value_or(observedMetrics.wallTimeMilliseconds + 1)
            <= observedMetrics.wallTimeMilliseconds);
    QCOMPARE(events.size(), std::size_t{3});
    QCOMPARE(events.at(0).type, AiStreamEventType::responseStarted);
    QCOMPARE(events.at(1).type, AiStreamEventType::textDelta);
    QCOMPARE(events.at(2).type, AiStreamEventType::responseCompleted);
}

void AiTurnRunnerTests::doesNotReplayAfterVisibleOutput()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{.payload =
                                     "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n\n"
                                     "data: {\"type\":\"response.output_text.delta\",\"delta\":\"partial\"}\n\n"});
    network.enqueue(
        FakeResponse{.payload = "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_2\"}}\n\n"});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    bool finished = false;

    QVERIFY(runner
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto, const AiTurnMetrics &) {
                        finished = true;
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(network.requestCount(), std::size_t{1});
    QCOMPARE(events.size(), std::size_t{3});
    QCOMPARE(events.at(0).type, AiStreamEventType::responseStarted);
    QCOMPARE(events.at(1).type, AiStreamEventType::textDelta);
    QCOMPARE(events.at(2).type, AiStreamEventType::responseFailed);
    QCOMPARE(events.at(2).error.value_or(AiProviderError{}).code, AiProviderErrorCode::protocol);
}

void AiTurnRunnerTests::cancelsScheduledRetry()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{.status = 429, .retryAfter = "1"});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    bool cancelled = false;
    bool finished = false;

    QVERIFY(runner
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto, const AiTurnMetrics &) {
                        finished = true;
                    },
                    [&runner, &cancelled](const auto, const auto, const auto) {
                        cancelled = runner.cancel();
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QVERIFY(cancelled);
    QCOMPARE(network.requestCount(), std::size_t{1});
    QCOMPARE(events.size(), std::size_t{1});
    QCOMPARE(events.front().type, AiStreamEventType::responseFailed);
    QCOMPARE(events.front().error.value_or(AiProviderError{}).code, AiProviderErrorCode::cancelled);
}

void AiTurnRunnerTests::cancelsActiveRequestSynchronously()
{
    FakeNetworkAccessManager network;
    network.enqueue(
        FakeResponse{.payload = "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"late\"}}\n\n",
                     .delayMilliseconds = 30'000});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    int finishedCount = 0;

    QVERIFY(runner
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finishedCount](const auto, const AiTurnMetrics &) {
                        ++finishedCount;
                    })
                .has_value());
    QVERIFY(runner.active());
    QVERIFY(runner.cancel());
    QVERIFY(!runner.active());
    QCOMPARE(finishedCount, 1);
    QVERIFY(!events.empty());
    QCOMPARE(events.back().type, AiStreamEventType::responseFailed);
    QCOMPARE(events.back().error.value_or(AiProviderError{}).code, AiProviderErrorCode::cancelled);

    QTest::qWait(10);
    QCOMPARE(finishedCount, 1);
}

void AiTurnRunnerTests::destroyingRunnerCancelsSafely()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{.payload = "data: ignored\n\n", .delayMilliseconds = 100});
    ProviderHttpClient client(&network);
    {
        AiTurnRunner runner(client, fastRetryPolicy());
        QVERIFY(runner
                    .start(
                        openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                        [](const auto, const AiStreamEvent &) {
                        },
                        [](const auto, const AiTurnMetrics &) {
                        })
                    .has_value());
    }
    QTest::qWait(150);
    QCOMPARE(network.requestCount(), std::size_t{1});
}

void AiTurnRunnerTests::executesReadToolAndContinuesTheSameTurn()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .payload =
            "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n\n"
            "data: "
            "{\"type\":\"response.output_item.added\",\"item\":{\"type\":\"function_call\",\"id\":\"item_1\",\"call_"
            "id\":\"call_1\",\"name\":\"read_session_info\"}}\n\n"
            "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"item_1\",\"arguments\":\"{}\"}\n\n"
            "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\"}}\n\n"});
    network.enqueue(FakeResponse{.payload =
                                     "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_2\"}}\n\n"
                                     "data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                                     "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_2\"}}\n\n"});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    bool finished = false;
    std::size_t toolCalls = 0;
    std::size_t observedToolOutputs = 0;

    QVERIFY(runner
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto, const AiTurnMetrics &) {
                        finished = true;
                    },
                    {}, {},
                    [&toolCalls](const ztermy::ai::AiToolCall &call)
                        -> std::expected<AiTurnRunner::ToolHandlingResult, AiProviderError> {
                        ++toolCalls;
                        return AiTurnRunner::ToolHandlingResult{
                            .output =
                                AiToolOutput{.callId = call.id, .name = call.name, .outputJson = R"({"ok":true})"}};
                    },
                    [&observedToolOutputs](const ztermy::ai::AiToolCall &call, const AiToolOutput &output) {
                        ++observedToolOutputs;
                        QCOMPARE(call.id, output.callId);
                        QCOMPARE(call.name, output.name);
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(network.requestCount(), std::size_t{2});
    QCOMPARE(toolCalls, std::size_t{1});
    QCOMPARE(observedToolOutputs, std::size_t{1});
    QCOMPARE(events.back().type, AiStreamEventType::responseCompleted);
    QCOMPARE(events.at(events.size() - 2).delta, std::string("done"));
}

void AiTurnRunnerTests::waitsForDeferredToolCompletion()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .payload =
            "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n\n"
            "data: "
            "{\"type\":\"response.output_item.added\",\"item\":{\"type\":\"function_call\",\"id\":\"item_1\",\"call_"
            "id\":\"call_1\",\"name\":\"run_command\"}}\n\n"
            "data: "
            "{\"type\":\"response.function_call_arguments.done\",\"item_id\":\"item_1\",\"arguments\":\"{"
            "\\\"command\\\":\\\"pwd\\\"}\"}\n\n"
            "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\"}}\n\n"});
    network.enqueue(FakeResponse{.payload =
                                     "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_2\"}}\n\n"
                                     "data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                                     "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_2\"}}\n\n"});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    bool finished = false;

    QVERIFY(
        runner
            .start(
                openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                [](const auto, const AiStreamEvent &) {
                },
                [&finished](const auto, const AiTurnMetrics &) {
                    finished = true;
                },
                {}, {},
                [](const ztermy::ai::AiToolCall &) -> std::expected<AiTurnRunner::ToolHandlingResult, AiProviderError> {
                    return AiTurnRunner::ToolHandlingResult{.sideEffecting = true};
                })
            .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(runner.pendingToolCall().has_value(), 1000);
    QCOMPARE(network.requestCount(), std::size_t{1});
    const auto pending = runner.pendingToolCall().value_or(ztermy::ai::AiToolCall{});
    QVERIFY(runner.completePendingTool(
        AiToolOutput{.callId = pending.id, .name = pending.name, .outputJson = R"({"ok":true})"}));
    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(network.requestCount(), std::size_t{2});
}

void AiTurnRunnerTests::cancelsDeferredToolWait()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .payload =
            "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n\n"
            "data: "
            "{\"type\":\"response.output_item.added\",\"item\":{\"type\":\"function_call\",\"id\":\"item_1\",\"call_"
            "id\":\"call_1\",\"name\":\"run_command\"}}\n\n"
            "data: "
            "{\"type\":\"response.function_call_arguments.done\",\"item_id\":\"item_1\",\"arguments\":\"{"
            "\\\"command\\\":\\\"sleep 30\\\"}\"}\n\n"
            "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\"}}\n\n"});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    bool cancellationCalled = false;
    bool finished = false;

    QVERIFY(runner
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto, const AiTurnMetrics &) {
                        finished = true;
                    },
                    {}, {},
                    [&cancellationCalled](const ztermy::ai::AiToolCall &)
                        -> std::expected<AiTurnRunner::ToolHandlingResult, AiProviderError> {
                        return AiTurnRunner::ToolHandlingResult{.cancel =
                                                                    [&cancellationCalled] {
                                                                        cancellationCalled = true;
                                                                    },
                                                                .sideEffecting = true};
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(runner.pendingToolCall().has_value(), 1000);
    QVERIFY(runner.cancel());
    QVERIFY(cancellationCalled);
    QVERIFY(finished);
    QCOMPARE(events.back().type, AiStreamEventType::responseFailed);
    QCOMPARE(events.back().error.value_or(AiProviderError{}).code, AiProviderErrorCode::cancelled);
}

void AiTurnRunnerTests::doesNotRetryAfterSideEffectingTool()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .payload =
            "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n\n"
            "data: "
            "{\"type\":\"response.output_item.added\",\"item\":{\"type\":\"function_call\",\"id\":\"item_1\",\"call_"
            "id\":\"call_1\",\"name\":\"run_command\"}}\n\n"
            "data: "
            "{\"type\":\"response.function_call_arguments.done\",\"item_id\":\"item_1\",\"arguments\":\"{"
            "\\\"command\\\":\\\"touch marker\\\"}\"}\n\n"
            "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\"}}\n\n"});
    network.enqueue(FakeResponse{.status = 500, .payload = "temporary"});
    network.enqueue(
        FakeResponse{.payload = "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_3\"}}\n\n"});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    bool finished = false;

    QVERIFY(runner
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto, const AiTurnMetrics &) {
                        finished = true;
                    },
                    {}, {},
                    [](const ztermy::ai::AiToolCall &call)
                        -> std::expected<AiTurnRunner::ToolHandlingResult, AiProviderError> {
                        return AiTurnRunner::ToolHandlingResult{
                            .output =
                                AiToolOutput{.callId = call.id, .name = call.name, .outputJson = R"({"ok":true})"},
                            .sideEffecting = true};
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(network.requestCount(), std::size_t{2});
    QCOMPARE(events.back().type, AiStreamEventType::responseFailed);
}

void AiTurnRunnerTests::rejectsOversizedCompletedToolArguments()
{
    // The full-arguments path (response.function_call_arguments.done) must be
    // held to the same 16 KiB limit as incremental deltas; otherwise a
    // provider could smuggle an unbounded argument blob to tool execution.
    const std::string oversizedArguments(std::string::size_type{17} * 1024, 'a');
    std::string payload = "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n\n"
                          "data: {\"type\":\"response.output_item.added\",\"item\":{\"type\":\"function_"
                          "call\",\"id\":\"item_1\",\"call_id\":\"call_1\",\"name\":\"run_command\"}}\n\n"
                          "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"item_1\","
                          "\"arguments\":\""
                          + oversizedArguments
                          + "\"}\n\n"
                            "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\"}}\n\n";
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{.payload = QByteArray::fromStdString(payload)});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    bool finished = false;
    std::size_t toolCalls = 0;

    QVERIFY(runner
                .start(
                    openAiConfiguration(), AiGenerationRequest{}, emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto, const AiTurnMetrics &) {
                        finished = true;
                    },
                    {}, {},
                    [&toolCalls](const ztermy::ai::AiToolCall &)
                        -> std::expected<AiTurnRunner::ToolHandlingResult, AiProviderError> {
                        ++toolCalls;
                        return AiTurnRunner::ToolHandlingResult{
                            .output = AiToolOutput{.callId = "call_1", .name = "run_command", .outputJson = "{}"}};
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(toolCalls, std::size_t{0});
    QCOMPARE(events.back().type, AiStreamEventType::responseFailed);
    QVERIFY(events.back().error.has_value());
    const AiProviderError error = events.back().error.value_or(AiProviderError{});
    QCOMPARE(error.code, AiProviderErrorCode::protocol);
    QVERIFY(QString::fromStdString(error.message).contains(QStringLiteral("16 KiB")));
}

void AiTurnRunnerTests::compactsAndRetriesOnContextOverflow()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{.status = 413, .payload = "request entity too large"});
    network.enqueue(FakeResponse{.payload =
                                     "data: {\"type\":\"response.created\",\"response\":{\"id\":\"resp_1\"}}\n\n"
                                     "data: {\"type\":\"response.output_text.delta\",\"delta\":\"ok\"}\n\n"
                                     "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_1\"}}\n\n"});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    bool finished = false;
    std::size_t compactedItems = 0;
    std::size_t removedBytes = 0;

    AiGenerationRequest generation;
    // 8 x 80k-char messages: over the tighter 413 retry budget so the
    // compaction pass actually truncates, while the preserved recent tail
    // still fits the budget afterwards.
    const std::string longMessage(80'000, 'x');
    for (int index = 0; index < 8; ++index)
    {
        generation.messages.push_back(
            ztermy::ai::AiChatMessage{.role = ztermy::ai::AiMessageRole::user, .content = longMessage});
    }

    QVERIFY(runner
                .start(
                    openAiConfiguration(), std::move(generation), emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto, const AiTurnMetrics &) {
                        finished = true;
                    },
                    {}, {}, {}, {},
                    [&compactedItems, &removedBytes](const auto, const ztermy::ai::AiCompactionResult &result) {
                        compactedItems += result.compactedItemCount;
                        removedBytes += result.removedBytes;
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(network.requestCount(), std::size_t{2});
    QCOMPARE(network.requestBodySizes().size(), std::size_t{2});
    // The retried payload must be smaller: the 413 forced a tighter
    // compaction of the request view.
    QVERIFY(network.requestBodySizes().at(1) < network.requestBodySizes().at(0));
    QVERIFY(compactedItems > 0);
    QVERIFY(removedBytes > 0);
    QCOMPARE(events.back().type, AiStreamEventType::responseCompleted);
}

void AiTurnRunnerTests::continuesAnthropicPausedTurnWithOpaqueContent()
{
    FakeNetworkAccessManager network;
    network.enqueue(FakeResponse{
        .payload =
            "event: message_start\n"
            "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_pause\",\"usage\":{\"input_tokens\":4}}}\n\n"
            "event: content_block_start\n"
            "data: "
            "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"server_tool_use\",\"id\":"
            "\"srvtoolu_1\",\"name\":\"web_search\",\"input\":{}}}\n\n"
            "event: content_block_delta\n"
            "data: "
            "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":"
            "\"{\\\"query\\\":\\\"Qt 6.8\\\"}\"}}\n\n"
            "event: content_block_stop\n"
            "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
            "event: content_block_start\n"
            "data: "
            "{\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"web_search_tool_result\","
            "\"tool_use_id\":\"srvtoolu_1\",\"content\":[{\"type\":\"web_search_result\",\"url\":\"https://"
            "example.test\",\"encrypted_content\":\"opaque-result\"}]}}\n\n"
            "event: message_delta\n"
            "data: "
            "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"pause_turn\"},\"usage\":{\"output_tokens\":8}}"
            "\n\n"
            "event: message_stop\n"
            "data: {\"type\":\"message_stop\"}\n\n"});
    network.enqueue(FakeResponse{
        .payload =
            "event: message_start\n"
            "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_final\",\"usage\":{\"input_tokens\":6}}}\n\n"
            "event: content_block_start\n"
            "data: "
            "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
            "event: content_block_delta\n"
            "data: "
            "{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"finished\"}}"
            "\n\n"
            "event: content_block_stop\n"
            "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
            "event: message_delta\n"
            "data: "
            "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":3}}\n\n"
            "event: message_stop\n"
            "data: {\"type\":\"message_stop\"}\n\n"});
    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    bool finished = false;
    const AiGenerationRequest generation{.messages = {{.content = "Search current Qt documentation."}},
                                         .webSearchEnabled = true};

    QVERIFY(runner
                .start(
                    anthropicConfiguration(), generation, emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto, const AiTurnMetrics &) {
                        finished = true;
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(network.requestCount(), std::size_t{2});
    QCOMPARE(std::ranges::count(events, AiStreamEventType::responseCompleted, &AiStreamEvent::type), std::ptrdiff_t{1});
    QCOMPARE(events.back().type, AiStreamEventType::responseCompleted);
    QCOMPARE(events.back().stopReason, ztermy::ai::AiResponseStopReason::endTurn);
    QCOMPARE(events.back().providerToolHistory.size(), std::size_t{1});
    QVERIFY(events.back().providerToolHistory.front().providerAssistantContentJson.find("opaque-result")
            != std::string::npos);
    QVERIFY(events.back().providerAssistantContentJson.find("finished") != std::string::npos);
    QVERIFY(network.requestBodies().size() >= 2);
    const auto continuation = QJsonDocument::fromJson(network.requestBodies().at(1)).object();
    const auto messages = continuation.value("messages").toArray();
    QCOMPARE(messages.size(), 2);
    const auto pausedContent = messages.at(1).toObject().value("content").toArray();
    QCOMPARE(pausedContent.size(), 2);
    QCOMPARE(pausedContent.at(0).toObject().value("input").toObject().value("query").toString(),
             QStringLiteral("Qt 6.8"));
    QCOMPARE(pausedContent.at(1)
                 .toObject()
                 .value("content")
                 .toArray()
                 .at(0)
                 .toObject()
                 .value("encrypted_content")
                 .toString(),
             QStringLiteral("opaque-result"));
}

void AiTurnRunnerTests::boundsAnthropicPauseContinuations()
{
    FakeNetworkAccessManager network;
    const QByteArray paused =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_pause\",\"usage\":{\"input_tokens\":1}}}\n\n"
        "event: content_block_start\n"
        "data: "
        "{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"server_tool_use\",\"id\":"
        "\"srvtoolu_pause\",\"name\":\"web_search\",\"input\":{\"query\":\"status\"}}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\n"
        "data: "
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"pause_turn\"},\"usage\":{\"output_tokens\":1}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n";
    for (int index = 0; index < 5; ++index)
    {
        network.enqueue(FakeResponse{.payload = paused});
    }

    ProviderHttpClient client(&network);
    AiTurnRunner runner(client, fastRetryPolicy());
    std::vector<AiStreamEvent> events;
    bool finished = false;
    QVERIFY(runner
                .start(
                    anthropicConfiguration(),
                    AiGenerationRequest{.messages = {{.content = "Search extensively."}}, .webSearchEnabled = true},
                    emptySecretLoader(),
                    [&events](const auto, const AiStreamEvent &event) {
                        events.push_back(event);
                    },
                    [&finished](const auto, const AiTurnMetrics &) {
                        finished = true;
                    })
                .has_value());

    QTRY_VERIFY_WITH_TIMEOUT(finished, 1000);
    QCOMPARE(network.requestCount(), std::size_t{5});
    QVERIFY(!events.empty());
    QCOMPARE(events.back().type, AiStreamEventType::responseFailed);
    QCOMPARE(events.back().error.value_or(AiProviderError{}).code, AiProviderErrorCode::protocol);
    QVERIFY(QString::fromStdString(events.back().error.value_or(AiProviderError{}).message)
                .contains(QStringLiteral("continuation limit")));
}

} // namespace

QTEST_GUILESS_MAIN(AiTurnRunnerTests)

#include "ai_turn_runner_tests.moc"
