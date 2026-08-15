#include "infrastructure/ai/AcpSessionUpdateMapper.h"

#include <QJsonArray>
#include <QtTest/QTest>

namespace
{

[[nodiscard]] ztermy::ai::AcpMessage update(QJsonObject payload)
{
    return {.kind = ztermy::ai::AcpMessageKind::notification,
            .method = QStringLiteral("session/update"),
            .params = QJsonObject{{QStringLiteral("sessionId"), QStringLiteral("session-1")},
                                  {QStringLiteral("update"), std::move(payload)}}};
}

class AcpSessionUpdateMapperTests final : public QObject
{
    Q_OBJECT

private slots:
    void mapsAssistantTextAndPublicThought();
    void preservesToolLifecycleAsTypedActivity();
    void keepsContextUsageSeparateFromTokenUsage();
    void rejectsMalformedKnownUpdatesAndIgnoresFutureMetadata();
};

void AcpSessionUpdateMapperTests::mapsAssistantTextAndPublicThought()
{
    ztermy::ai::AcpSessionUpdateMapper mapper;
    auto message = mapper.map(update(QJsonObject{
        {QStringLiteral("sessionUpdate"), QStringLiteral("agent_message_chunk")},
        {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                {QStringLiteral("text"), QStringLiteral("hello")}}},
    }));
    QVERIFY(message.has_value());
    QCOMPARE(message->streamEvents.size(), std::size_t{1});
    QCOMPARE(message->streamEvents.front().type, ztermy::ai::AiStreamEventType::textDelta);
    QCOMPARE(message->streamEvents.front().delta, std::string{"hello"});

    auto thought = mapper.map(update(QJsonObject{
        {QStringLiteral("sessionUpdate"), QStringLiteral("agent_thought_chunk")},
        {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                {QStringLiteral("text"), QStringLiteral("checking")}}},
    }));
    QVERIFY(thought.has_value());
    QCOMPARE(thought->streamEvents.front().type, ztermy::ai::AiStreamEventType::reasoningDelta);
    QCOMPARE(thought->streamEvents.front().delta, std::string{"checking"});
}

void AcpSessionUpdateMapperTests::preservesToolLifecycleAsTypedActivity()
{
    ztermy::ai::AcpSessionUpdateMapper mapper;
    auto started = mapper.map(update(QJsonObject{
        {QStringLiteral("sessionUpdate"), QStringLiteral("tool_call")},
        {QStringLiteral("toolCallId"), QStringLiteral("tool-1")},
        {QStringLiteral("title"), QStringLiteral("Inspect disk")},
        {QStringLiteral("kind"), QStringLiteral("execute")},
        {QStringLiteral("status"), QStringLiteral("in_progress")},
        {QStringLiteral("rawInput"), QJsonObject{{QStringLiteral("command"), QStringLiteral("df -h")}}},
    }));
    QVERIFY(started.has_value());
    if (!started->toolActivity.has_value())
    {
        QTest::qFail("The initial ACP tool call was not mapped.", __FILE__, __LINE__);
        return;
    }
    const auto &startedActivity = started->toolActivity.value();
    QCOMPARE(startedActivity.id, std::string{"tool-1"});
    QCOMPARE(startedActivity.summary, std::string{"Inspect disk"});
    QCOMPARE(startedActivity.name, std::string{"execute"});
    QCOMPARE(startedActivity.state, std::string{"running"});
    QVERIFY(startedActivity.sideEffecting);
    QCOMPARE(startedActivity.argumentsJson, std::string{R"({"command":"df -h"})"});

    auto completed = mapper.map(update(QJsonObject{
        {QStringLiteral("sessionUpdate"), QStringLiteral("tool_call_update")},
        {QStringLiteral("toolCallId"), QStringLiteral("tool-1")},
        {QStringLiteral("status"), QStringLiteral("completed")},
        {QStringLiteral("rawOutput"), QJsonObject{{QStringLiteral("exitCode"), 0}}},
    }));
    QVERIFY(completed.has_value());
    if (!completed->toolActivity.has_value())
    {
        QTest::qFail("The ACP tool completion was not mapped.", __FILE__, __LINE__);
        return;
    }
    const auto &completedActivity = completed->toolActivity.value();
    QCOMPARE(completedActivity.state, std::string{"succeeded"});
    QCOMPARE(completedActivity.summary, std::string{"Inspect disk"});
    QCOMPARE(completedActivity.resultJson, std::string{R"({"exitCode":0})"});
}

void AcpSessionUpdateMapperTests::keepsContextUsageSeparateFromTokenUsage()
{
    ztermy::ai::AcpSessionUpdateMapper mapper;
    auto mapped = mapper.map(update(QJsonObject{
        {QStringLiteral("sessionUpdate"), QStringLiteral("usage_update")},
        {QStringLiteral("used"), 120},
        {QStringLiteral("size"), 1000},
        {QStringLiteral("cost"),
         QJsonObject{{QStringLiteral("amount"), 0.05}, {QStringLiteral("currency"), QStringLiteral("USD")}}},
    }));
    QVERIFY(mapped.has_value());
    if (!mapped->usage.has_value() || !mapped->usage->costAmount.has_value())
    {
        QTest::qFail("The ACP usage update was not mapped completely.", __FILE__, __LINE__);
        return;
    }
    const auto &usage = mapped->usage.value();
    QCOMPARE(usage.used, std::uint64_t{120});
    QCOMPARE(usage.size, std::uint64_t{1000});
    QCOMPARE(usage.costAmount.value(), 0.05);
    QCOMPARE(usage.costCurrency, QStringLiteral("USD"));
    QVERIFY(mapped->streamEvents.empty());
}

void AcpSessionUpdateMapperTests::rejectsMalformedKnownUpdatesAndIgnoresFutureMetadata()
{
    ztermy::ai::AcpSessionUpdateMapper mapper;
    QVERIFY(!mapper
                 .map(update(QJsonObject{
                     {QStringLiteral("sessionUpdate"), QStringLiteral("tool_call_update")},
                     {QStringLiteral("toolCallId"), QStringLiteral("missing")},
                     {QStringLiteral("status"), QStringLiteral("completed")},
                 }))
                 .has_value());
    QVERIFY(!mapper
                 .map(update(QJsonObject{
                     {QStringLiteral("sessionUpdate"), QStringLiteral("usage_update")},
                     {QStringLiteral("used"), 1001},
                     {QStringLiteral("size"), 1000},
                 }))
                 .has_value());

    auto future = mapper.map(update(QJsonObject{
        {QStringLiteral("sessionUpdate"), QStringLiteral("future_metadata_update")},
        {QStringLiteral("value"), QJsonArray{}},
    }));
    QVERIFY(future.has_value());
    QVERIFY(future->streamEvents.empty());
    QVERIFY(!future->toolActivity.has_value());
}

} // namespace

QTEST_GUILESS_MAIN(AcpSessionUpdateMapperTests)

#include "acp_session_update_mapper_tests.moc"
