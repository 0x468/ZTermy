#include "infrastructure/ai/AcpClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonObject>
#include <QTimer>
#include <QtTest/QTest>

namespace
{

[[nodiscard]] QString testAgentPath()
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_acp_test_agent.exe");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_acp_test_agent");
#endif
}

[[nodiscard]] ztermy::ai::AcpClientConfiguration configuration(const QStringList &arguments = {})
{
    return {.program = testAgentPath(),
            .arguments = arguments,
            .workingDirectory = QDir::tempPath(),
            .clientVersion = "0.3.0-test"};
}

class AcpClientTests final : public QObject
{
    Q_OBJECT

private slots:
    void startsSessionStreamsAndCompletesClientRequest();
    void resumesAndCancelsOwnedSession();
    void rejectsForeignSessionUpdate();
    void deduplicatesAgentRequestsWithinPrompt();
};

void AcpClientTests::startsSessionStreamsAndCompletesClientRequest()
{
    ztermy::ai::AcpClient client;
    QEventLoop readyLoop;
    QEventLoop promptLoop;
    QString failure;
    bool sawThought = false;
    bool sawText = false;
    bool sawTool = false;
    bool sawUsage = false;
    bool handledRequest = false;
    std::optional<ztermy::ai::AcpClientReady> ready;
    std::optional<ztermy::ai::AcpPromptCompletion> completion;

    const auto started = client.start(
        configuration(),
        [&readyLoop, &failure, &ready](auto result) {
            if (result.has_value())
            {
                ready = std::move(*result);
            }
            else
            {
                failure = result.error();
            }
            readyLoop.quit();
        },
        [&promptLoop, &failure, &sawThought, &sawText, &sawTool, &sawUsage](auto result) {
            if (!result.has_value())
            {
                failure = result.error();
                promptLoop.quit();
                return;
            }
            const QJsonObject update = result->params.value(QStringLiteral("update")).toObject();
            const QString type = update.value(QStringLiteral("sessionUpdate")).toString();
            sawThought = sawThought || type == QStringLiteral("agent_thought_chunk");
            sawText = sawText || type == QStringLiteral("agent_message_chunk");
            sawTool = sawTool || type == QStringLiteral("tool_call_update");
            sawUsage = sawUsage || type == QStringLiteral("usage_update");
        },
        [&client, &failure, &handledRequest](const ztermy::ai::AcpMessage &request) {
            handledRequest = request.method == QStringLiteral("terminal/create")
                             && request.params.value(QStringLiteral("sessionId")).toString() == client.sessionId();
            const auto completed = client.completeRequest(
                request.id, QJsonObject{{QStringLiteral("terminalId"), QStringLiteral("terminal-owning-tab")}});
            if (!completed.has_value())
            {
                failure = completed.error();
            }
        });
    QVERIFY(started.has_value());
    QTimer::singleShot(5'000, &readyLoop, &QEventLoop::quit);
    readyLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    if (!ready.has_value())
    {
        QTest::qFail("The ACP Client did not publish ready metadata.", __FILE__, __LINE__);
        return;
    }
    const auto &readyValue = ready.value();
    QCOMPARE(readyValue.sessionId, QStringLiteral("session-ztermy"));
    QCOMPARE(readyValue.agentName, QStringLiteral("ztermy-test-agent"));
    QCOMPARE(readyValue.agentVersion, QStringLiteral("1.0"));
    QVERIFY(readyValue.agentCapabilities.value(QStringLiteral("loadSession")).toBool());
    QVERIFY(readyValue.sessionMetadata.value(QStringLiteral("configOptions")).isArray());
    QVERIFY(client.ready());

    const auto prompt =
        client.startPrompt("Inspect the owning terminal", [&promptLoop, &failure, &completion](auto result) {
            if (result.has_value())
            {
                completion = std::move(*result);
            }
            else
            {
                failure = result.error();
            }
            promptLoop.quit();
        });
    QVERIFY(prompt.has_value());
    QTimer::singleShot(5'000, &promptLoop, &QEventLoop::quit);
    promptLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    if (!completion.has_value())
    {
        QTest::qFail("The ACP prompt did not publish a completion.", __FILE__, __LINE__);
        return;
    }
    const auto &completionValue = completion.value();
    QCOMPARE(completionValue.stopReason, QStringLiteral("end_turn"));
    QVERIFY(!completionValue.cancellationRequested);
    QVERIFY(sawThought);
    QVERIFY(sawText);
    QVERIFY(sawTool);
    QVERIFY(sawUsage);
    QVERIFY(handledRequest);
    QVERIFY(client.ready());
    client.stop();
}

void AcpClientTests::resumesAndCancelsOwnedSession()
{
    ztermy::ai::AcpClient client;
    QEventLoop readyLoop;
    QEventLoop promptLoop;
    QString failure;
    bool resumed = false;
    auto config = configuration({QStringLiteral("--cancel")});
    config.resumeSessionId = "session-resumed";
    const auto started =
        client.start(std::move(config),
                     [&readyLoop, &failure, &resumed](auto result) {
                         if (result.has_value())
                         {
                             resumed = result->resumed && result->sessionId == QStringLiteral("session-resumed");
                         }
                         else
                         {
                             failure = result.error();
                         }
                         readyLoop.quit();
                     },
                     {}, {});
    QVERIFY(started.has_value());
    QTimer::singleShot(5'000, &readyLoop, &QEventLoop::quit);
    readyLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(resumed);

    std::optional<ztermy::ai::AcpPromptCompletion> completion;
    QVERIFY(client
                .startPrompt("Wait",
                             [&promptLoop, &failure, &completion](auto result) {
                                 if (result.has_value())
                                 {
                                     completion = std::move(*result);
                                 }
                                 else
                                 {
                                     failure = result.error();
                                 }
                                 promptLoop.quit();
                             })
                .has_value());
    QVERIFY(client.cancelPrompt().has_value());
    QVERIFY(client.cancelPrompt().has_value());
    QTimer::singleShot(5'000, &promptLoop, &QEventLoop::quit);
    promptLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    if (!completion.has_value())
    {
        QTest::qFail("The cancelled ACP prompt did not publish a completion.", __FILE__, __LINE__);
        return;
    }
    const auto &completionValue = completion.value();
    QCOMPARE(completionValue.stopReason, QStringLiteral("cancelled"));
    QVERIFY(completionValue.cancellationRequested);
    QVERIFY(client.ready());
    client.stop();
}

void AcpClientTests::rejectsForeignSessionUpdate()
{
    ztermy::ai::AcpClient client;
    QEventLoop readyLoop;
    QEventLoop promptLoop;
    QString failure;
    QVERIFY(client
                .start(configuration({QStringLiteral("--foreign-update")}),
                       [&readyLoop, &failure](auto result) {
                           if (!result.has_value())
                           {
                               failure = result.error();
                           }
                           readyLoop.quit();
                       },
                       {}, {})
                .has_value());
    QTimer::singleShot(5'000, &readyLoop, &QEventLoop::quit);
    readyLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    QVERIFY(client
                .startPrompt("Cross the boundary",
                             [&promptLoop, &failure](auto result) {
                                 if (!result.has_value())
                                 {
                                     failure = result.error();
                                 }
                                 promptLoop.quit();
                             })
                .has_value());
    QTimer::singleShot(5'000, &promptLoop, &QEventLoop::quit);
    promptLoop.exec();
    QVERIFY(failure.contains(QStringLiteral("owning session")));
    QCOMPARE(client.state(), ztermy::ai::AcpClient::State::failed);
    client.stop();
}

void AcpClientTests::deduplicatesAgentRequestsWithinPrompt()
{
    ztermy::ai::AcpClient client;
    QEventLoop readyLoop;
    QEventLoop promptLoop;
    QString failure;
    int dispatchCount = 0;
    QVERIFY(client
                .start(
                    configuration({QStringLiteral("--duplicate-request")}),
                    [&readyLoop, &failure](auto result) {
                        if (!result.has_value())
                        {
                            failure = result.error();
                        }
                        readyLoop.quit();
                    },
                    {},
                    [&client, &failure, &dispatchCount](const ztermy::ai::AcpMessage &request) {
                        ++dispatchCount;
                        const auto completed =
                            client.completeRequest(request.id, QJsonObject{{QStringLiteral("ok"), true}});
                        if (!completed.has_value())
                        {
                            failure = completed.error();
                        }
                    })
                .has_value());
    QTimer::singleShot(5'000, &readyLoop, &QEventLoop::quit);
    readyLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(client
                .startPrompt("Run once",
                             [&promptLoop, &failure](auto result) {
                                 if (!result.has_value())
                                 {
                                     failure = result.error();
                                 }
                                 promptLoop.quit();
                             })
                .has_value());
    QTimer::singleShot(5'000, &promptLoop, &QEventLoop::quit);
    promptLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(dispatchCount, 1);
    QVERIFY(client.ready());
    client.stop();
}

} // namespace

QTEST_GUILESS_MAIN(AcpClientTests)

#include "acp_client_tests.moc"
