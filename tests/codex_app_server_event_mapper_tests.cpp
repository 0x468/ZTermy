#include "infrastructure/ai/CodexAppServerEventMapper.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QTest>

#include <string>
#include <utility>

namespace
{

[[nodiscard]] ztermy::ai::CodexAppServerMessage notification(const QString &method, QJsonObject params)
{
    return {.kind = ztermy::ai::CodexAppServerMessageKind::notification, .method = method, .params = std::move(params)};
}

class CodexAppServerEventMapperTests final : public QObject
{
    Q_OBJECT

private slots:
    void mapsTextReasoningUsageAndCompletion();
    void prefersReasoningSummaryOverRawContent();
    void mapsDynamicToolsAndSearchActivities();
    void mapsFailuresAndRejectsMalformedEvents();
};

void CodexAppServerEventMapperTests::mapsTextReasoningUsageAndCompletion()
{
    ztermy::ai::CodexAppServerEventMapper mapper;
    auto started = mapper.map(notification(
        QStringLiteral("turn/started"),
        QJsonObject{{QStringLiteral("turn"), QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-1")}}}}));
    QVERIFY(started.has_value());
    QCOMPARE(started->size(), std::size_t{1});
    QCOMPARE(started->front().type, ztermy::ai::AiStreamEventType::responseStarted);

    auto text = mapper.map(notification(QStringLiteral("item/agentMessage/delta"),
                                        QJsonObject{{QStringLiteral("itemId"), QStringLiteral("message-1")},
                                                    {QStringLiteral("delta"), QStringLiteral("hello")}}));
    QVERIFY(text.has_value());
    QCOMPARE(text->front().type, ztermy::ai::AiStreamEventType::textDelta);
    QCOMPARE(text->front().delta, std::string("hello"));

    auto raw = mapper.map(notification(QStringLiteral("item/reasoning/textDelta"),
                                       QJsonObject{{QStringLiteral("itemId"), QStringLiteral("reasoning-1")},
                                                   {QStringLiteral("delta"), QStringLiteral("inspect")}}));
    QVERIFY(raw.has_value());
    QVERIFY(raw->empty());
    auto reasoningCompleted = mapper.map(notification(
        QStringLiteral("item/completed"),
        QJsonObject{{QStringLiteral("item"), QJsonObject{{QStringLiteral("id"), QStringLiteral("reasoning-1")},
                                                         {QStringLiteral("type"), QStringLiteral("reasoning")}}}}));
    QVERIFY(reasoningCompleted.has_value());
    QCOMPARE(reasoningCompleted->front().type, ztermy::ai::AiStreamEventType::reasoningDelta);
    QCOMPARE(reasoningCompleted->front().delta, std::string("inspect"));

    auto tokenUsage = mapper.map(notification(
        QStringLiteral("thread/tokenUsage/updated"),
        QJsonObject{{QStringLiteral("tokenUsage"),
                     QJsonObject{{QStringLiteral("last"), QJsonObject{{QStringLiteral("inputTokens"), 10},
                                                                      {QStringLiteral("outputTokens"), 7},
                                                                      {QStringLiteral("reasoningOutputTokens"), 3},
                                                                      {QStringLiteral("cachedInputTokens"), 2}}}}}}));
    QVERIFY(tokenUsage.has_value());
    QVERIFY(tokenUsage->front().usage.has_value());
    const auto usage = tokenUsage->front().usage.value_or(ztermy::ai::AiTokenUsage{});
    QCOMPARE(usage.reasoningTokens, std::uint64_t{3});

    auto completed = mapper.map(notification(
        QStringLiteral("turn/completed"),
        QJsonObject{{QStringLiteral("turn"), QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-1")},
                                                         {QStringLiteral("status"), QStringLiteral("completed")}}}}));
    QVERIFY(completed.has_value());
    QCOMPARE(completed->front().type, ztermy::ai::AiStreamEventType::responseCompleted);
    QCOMPARE(completed->front().stopReason, ztermy::ai::AiResponseStopReason::endTurn);
}

void CodexAppServerEventMapperTests::prefersReasoningSummaryOverRawContent()
{
    ztermy::ai::CodexAppServerEventMapper mapper;
    QVERIFY(mapper
                .map(notification(QStringLiteral("turn/started"),
                                  QJsonObject{{QStringLiteral("turn"),
                                               QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-2")}}}}))
                .has_value());
    QVERIFY(mapper
                .map(notification(QStringLiteral("item/reasoning/textDelta"),
                                  QJsonObject{{QStringLiteral("itemId"), QStringLiteral("reasoning-2")},
                                              {QStringLiteral("delta"), QStringLiteral("raw secret")}}))
                .has_value());
    auto summary = mapper.map(notification(QStringLiteral("item/reasoning/summaryTextDelta"),
                                           QJsonObject{{QStringLiteral("itemId"), QStringLiteral("reasoning-2")},
                                                       {QStringLiteral("delta"), QStringLiteral("summary")}}));
    QVERIFY(summary.has_value());
    QCOMPARE(summary->front().delta, std::string("summary"));
    auto completed = mapper.map(notification(
        QStringLiteral("item/completed"),
        QJsonObject{{QStringLiteral("item"), QJsonObject{{QStringLiteral("id"), QStringLiteral("reasoning-2")},
                                                         {QStringLiteral("type"), QStringLiteral("reasoning")}}}}));
    QVERIFY(completed.has_value());
    QVERIFY(completed->empty());
}

void CodexAppServerEventMapperTests::mapsDynamicToolsAndSearchActivities()
{
    ztermy::ai::CodexAppServerEventMapper mapper;
    QVERIFY(mapper
                .map(notification(QStringLiteral("turn/started"),
                                  QJsonObject{{QStringLiteral("turn"),
                                               QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-tools")}}}}))
                .has_value());
    auto tool = mapper.map(
        notification(QStringLiteral("item/started"),
                     QJsonObject{{QStringLiteral("item"),
                                  QJsonObject{{QStringLiteral("id"), QStringLiteral("tool-1")},
                                              {QStringLiteral("type"), QStringLiteral("dynamicToolCall")},
                                              {QStringLiteral("tool"), QStringLiteral("run_command")},
                                              {QStringLiteral("arguments"),
                                               QJsonObject{{QStringLiteral("command"), QStringLiteral("df -h")}}}}}}));
    QVERIFY(tool.has_value());
    QCOMPARE(tool->size(), std::size_t{2});
    QCOMPARE((*tool)[0].type, ztermy::ai::AiStreamEventType::toolCallStarted);
    QCOMPARE((*tool)[1].type, ztermy::ai::AiStreamEventType::toolArgumentsDelta);

    auto toolCompleted = mapper.map(notification(
        QStringLiteral("item/completed"),
        QJsonObject{{QStringLiteral("item"), QJsonObject{{QStringLiteral("id"), QStringLiteral("tool-1")},
                                                         {QStringLiteral("type"), QStringLiteral("dynamicToolCall")},
                                                         {QStringLiteral("tool"), QStringLiteral("run_command")}}}}));
    QVERIFY(toolCompleted.has_value());
    QCOMPARE(toolCompleted->front().type, ztermy::ai::AiStreamEventType::toolCallCompleted);

    auto search = mapper.map(notification(
        QStringLiteral("item/started"),
        QJsonObject{{QStringLiteral("item"), QJsonObject{{QStringLiteral("id"), QStringLiteral("search-1")},
                                                         {QStringLiteral("type"), QStringLiteral("webSearch")},
                                                         {QStringLiteral("query"), QStringLiteral("Qt 6.8")}}}}));
    QVERIFY(search.has_value());
    QCOMPARE(search->size(), std::size_t{2});
    QCOMPARE((*search)[0].type, ztermy::ai::AiStreamEventType::webSearchStarted);
    QCOMPARE((*search)[1].type, ztermy::ai::AiStreamEventType::webSearchQuery);
}

void CodexAppServerEventMapperTests::mapsFailuresAndRejectsMalformedEvents()
{
    ztermy::ai::CodexAppServerEventMapper mapper;
    QVERIFY(mapper
                .map(notification(QStringLiteral("turn/started"),
                                  QJsonObject{{QStringLiteral("turn"),
                                               QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-3")}}}}))
                .has_value());
    QVERIFY(mapper
                .map(notification(QStringLiteral("error"),
                                  QJsonObject{{QStringLiteral("error"),
                                               QJsonObject{{QStringLiteral("message"), QStringLiteral("quota")},
                                                           {QStringLiteral("codexErrorInfo"),
                                                            QStringLiteral("usageLimitExceeded")}}}}))
                .has_value());
    auto failed = mapper.map(notification(
        QStringLiteral("turn/completed"),
        QJsonObject{{QStringLiteral("turn"), QJsonObject{{QStringLiteral("id"), QStringLiteral("turn-3")},
                                                         {QStringLiteral("status"), QStringLiteral("failed")}}}}));
    QVERIFY(failed.has_value());
    QVERIFY(failed->front().error.has_value());
    const auto error = failed->front().error.value_or(ztermy::ai::AiProviderError{});
    QCOMPARE(error.code, ztermy::ai::AiProviderErrorCode::quotaExceeded);

    QString oversized(qsizetype{256} * 1024 + 1, QChar(u'x'));
    QVERIFY(!mapper
                 .map(notification(QStringLiteral("item/agentMessage/delta"),
                                   QJsonObject{{QStringLiteral("itemId"), QStringLiteral("message")},
                                               {QStringLiteral("delta"), oversized}}))
                 .has_value());
}

} // namespace

QTEST_GUILESS_MAIN(CodexAppServerEventMapperTests)

#include "codex_app_server_event_mapper_tests.moc"
