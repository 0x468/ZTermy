#include "infrastructure/ai/CodexAppServerClient.h"
#include "infrastructure/ai/CodexAppServerDiscovery.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QTest>

#include <optional>
#include <string>
#include <vector>

namespace
{

[[nodiscard]] std::vector<ztermy::ai::AiToolDefinition> tools()
{
    return {{.name = "read_terminal_frame",
             .description = "Read the bounded frame of the owning ztermy terminal.",
             .parametersJson = R"({"type":"object","additionalProperties":false})"}};
}

[[nodiscard]] ztermy::ai::CodexAppServerConfiguration configuration(const QStringList &arguments = {})
{
    return {.program =
                QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_codex_app_server_test_server.exe"),
            .arguments = arguments,
            .workingDirectory = QDir::tempPath(),
            .model = "test-model",
            .clientVersion = "0.3.0",
            .developerInstructions = "Use only the owning ztermy terminal tools.",
            .tools = tools(),
            .dynamicToolsVerified = true};
}

class CodexAppServerClientTests final : public QObject
{
    Q_OBJECT

private slots:
    void startsTurnAndCompletesHostToolCall();
    void queuesImmediateInterruptUntilTurnIdArrives();
    void resumesThreadWithCurrentToolCatalog();
    void rejectsUnverifiedRuntime();
    void discoversExperimentalDynamicToolSchema();
    void connectsToInstalledCodexWhenEnabled();
};

void CodexAppServerClientTests::startsTurnAndCompletesHostToolCall()
{
    ztermy::ai::CodexAppServerClient client;
    QEventLoop readyLoop;
    QEventLoop turnLoop;
    QString failure;
    QString readyThread;
    bool sawDelta = false;
    bool sawTool = false;
    auto started = client.start(
        configuration(),
        [&readyLoop, &failure, &readyThread](auto result) {
            if (result.has_value())
            {
                readyThread = result->threadId;
            }
            else
            {
                failure = result.error();
            }
            readyLoop.quit();
        },
        [&turnLoop, &failure, &sawDelta](auto event) {
            if (!event.has_value())
            {
                failure = event.error();
                turnLoop.quit();
                return;
            }
            sawDelta = sawDelta || event->method == QStringLiteral("item/agentMessage/delta");
            if (event->method == QStringLiteral("turn/completed"))
            {
                turnLoop.quit();
            }
        },
        [&client, &failure, &sawTool](const ztermy::ai::CodexDynamicToolCall &call) {
            sawTool = true;
            if (call.tool != QStringLiteral("read_terminal_frame") || call.threadId != client.threadId())
            {
                failure = QStringLiteral("The tool call was not scoped to the owning thread.");
                return;
            }
            const auto completed = client.completeToolCall(call.requestId, true, R"({"ok":true})");
            if (!completed.has_value())
            {
                failure = completed.error();
            }
        });
    QVERIFY(started.has_value());
    QTimer::singleShot(5'000, &readyLoop, &QEventLoop::quit);
    readyLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(readyThread, QStringLiteral("thread-ztermy"));
    QVERIFY(client.ready());

    const auto turn = client.startTurn("Inspect the current terminal");
    QVERIFY(turn.has_value());
    QTimer::singleShot(5'000, &turnLoop, &QEventLoop::quit);
    turnLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(sawDelta);
    QVERIFY(sawTool);
    QVERIFY(client.ready());
    client.stop();
}

void CodexAppServerClientTests::queuesImmediateInterruptUntilTurnIdArrives()
{
    ztermy::ai::CodexAppServerClient client;
    QEventLoop readyLoop;
    QEventLoop turnLoop;
    QString failure;
    auto started = client.start(
        configuration({QStringLiteral("--no-tool")}),
        [&readyLoop, &failure](auto result) {
            if (!result.has_value())
            {
                failure = result.error();
            }
            readyLoop.quit();
        },
        [&turnLoop, &failure](auto event) {
            if (!event.has_value())
            {
                failure = event.error();
                turnLoop.quit();
            }
            else if (event->method == QStringLiteral("turn/completed"))
            {
                turnLoop.quit();
            }
        },
        {});
    QVERIFY(started.has_value());
    QTimer::singleShot(5'000, &readyLoop, &QEventLoop::quit);
    readyLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(client.startTurn("Wait").has_value());
    QVERIFY(client.interrupt().has_value());
    QVERIFY(client.interrupt().has_value());
    QTimer::singleShot(5'000, &turnLoop, &QEventLoop::quit);
    turnLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(client.ready());
    client.stop();
}

void CodexAppServerClientTests::resumesThreadWithCurrentToolCatalog()
{
    ztermy::ai::CodexAppServerClient client;
    QEventLoop readyLoop;
    QString failure;
    bool resumed = false;
    auto config = configuration();
    config.resumeThreadId = "thread-resumed";
    auto started = client.start(std::move(config),
                                [&readyLoop, &failure, &resumed](auto result) {
                                    if (result.has_value())
                                    {
                                        resumed =
                                            result->resumed && result->threadId == QStringLiteral("thread-resumed");
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
    client.stop();
}

void CodexAppServerClientTests::rejectsUnverifiedRuntime()
{
    ztermy::ai::CodexAppServerClient client;
    auto config = configuration();
    config.dynamicToolsVerified = false;
    QVERIFY(!client.start(std::move(config), {}, {}, {}).has_value());
    QCOMPARE(client.state(), ztermy::ai::CodexAppServerClient::State::stopped);
}

void CodexAppServerClientTests::discoversExperimentalDynamicToolSchema()
{
    const QString helper =
        QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_codex_app_server_test_server.exe");
    ztermy::ai::CodexAppServerDiscovery discovery;
    QEventLoop loop;
    std::optional<ztermy::ai::CodexAppServerInstallation> installation;
    QString failure;
    const auto started = discovery.start(helper, [&loop, &installation, &failure](auto result) {
        if (result.has_value())
        {
            installation = std::move(*result);
        }
        else
        {
            failure = result.error();
        }
        loop.quit();
    });
    QVERIFY(started.has_value());
    QTimer::singleShot(5'000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    if (!installation.has_value())
    {
        QFAIL("Codex discovery did not publish an installation.");
    }
    const ztermy::ai::CodexAppServerInstallation &discovered = *installation;
    QVERIFY(discovered.dynamicToolsVerified);
    QCOMPARE(discovered.version, QStringLiteral("codex-cli 999.0.0-test"));
    QVERIFY(!discovery.running());

    QTemporaryDir incomplete;
    QVERIFY(incomplete.isValid());
    QVERIFY(
        !ztermy::ai::CodexAppServerDiscovery::inspectGeneratedSchema(incomplete.path(), helper, QStringLiteral("test"))
             .has_value());
}

void CodexAppServerClientTests::connectsToInstalledCodexWhenEnabled()
{
    if (qgetenv("ZTERMY_TEST_CODEX_REAL") != QByteArrayLiteral("1"))
    {
        QSKIP("Set ZTERMY_TEST_CODEX_REAL=1 to run the installed Codex app-server smoke test");
    }

    const QString program = QStandardPaths::findExecutable(QStringLiteral("codex"));
    QVERIFY2(!program.isEmpty(), "Codex is not available on PATH");

    ztermy::ai::CodexAppServerDiscovery discovery;
    QEventLoop discoveryLoop;
    std::optional<ztermy::ai::CodexAppServerInstallation> installation;
    QString failure;
    const auto discoveryStarted = discovery.start(program, [&discoveryLoop, &installation, &failure](auto result) {
        if (result.has_value())
        {
            installation = std::move(*result);
        }
        else
        {
            failure = result.error();
        }
        discoveryLoop.quit();
    });
    if (!discoveryStarted.has_value())
    {
        QFAIL(qPrintable(discoveryStarted.error()));
    }
    QTimer::singleShot(35'000, &discoveryLoop, &QEventLoop::quit);
    discoveryLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY2(installation.has_value(), "Codex discovery timed out");
    QVERIFY(installation->dynamicToolsVerified);

    QTemporaryDir workingDirectory;
    QVERIFY(workingDirectory.isValid());
    ztermy::ai::CodexAppServerClient client;
    QEventLoop readyLoop;
    QEventLoop turnLoop;
    bool sawAgentDelta = false;
    bool sawCompletedAgentMessage = false;
    bool completed = false;
    QStringList methods;
    QJsonObject completion;
    auto started = client.start(
        {.program = installation->program,
         .arguments = {QStringLiteral("app-server")},
         .workingDirectory = workingDirectory.path(),
         .model = {},
         .clientVersion = "0.3.0-real-smoke",
         .developerInstructions = "Do not call tools. Reply with exactly ZTERMY_CODEX_REAL_READY.",
         .tools = tools(),
         .dynamicToolsVerified = true},
        [&readyLoop, &failure](auto result) {
            if (!result.has_value())
            {
                failure = result.error();
            }
            readyLoop.quit();
        },
        [&turnLoop, &failure, &sawAgentDelta, &sawCompletedAgentMessage, &completed, &methods,
         &completion](auto event) {
            if (!event.has_value())
            {
                failure = event.error();
                turnLoop.quit();
                return;
            }
            methods.push_back(event->method);
            sawAgentDelta = sawAgentDelta || event->method == QStringLiteral("item/agentMessage/delta");
            if (event->method == QStringLiteral("item/completed"))
            {
                const QJsonObject item = event->params.value(QStringLiteral("item")).toObject();
                sawCompletedAgentMessage =
                    sawCompletedAgentMessage
                    || (item.value(QStringLiteral("type")).toString() == QStringLiteral("agentMessage")
                        && !item.value(QStringLiteral("text")).toString().isEmpty());
            }
            if (event->method == QStringLiteral("turn/completed"))
            {
                completed = true;
                completion = event->params;
                turnLoop.quit();
            }
        },
        {});
    if (!started.has_value())
    {
        QFAIL(qPrintable(started.error()));
    }
    QTimer::singleShot(15'000, &readyLoop, &QEventLoop::quit);
    readyLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY2(client.ready(), "Codex app-server handshake timed out");

    const auto turn = client.startTurn("Reply with exactly ZTERMY_CODEX_REAL_READY.");
    if (!turn.has_value())
    {
        QFAIL(qPrintable(turn.error()));
    }
    QTimer::singleShot(90'000, &turnLoop, &QEventLoop::quit);
    turnLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY2(completed, "Codex app-server turn timed out");
    QVERIFY2(completion.value(QStringLiteral("turn")).toObject().value(QStringLiteral("status")).toString()
                 == QStringLiteral("completed"),
             qPrintable(QString::fromUtf8(QJsonDocument(completion).toJson(QJsonDocument::Compact))));
    QVERIFY2(sawAgentDelta || sawCompletedAgentMessage,
             qPrintable(QStringLiteral("Observed methods: %1").arg(methods.join(QLatin1Char(',')))));
    QVERIFY(client.ready());
    client.stop();
}

} // namespace

QTEST_GUILESS_MAIN(CodexAppServerClientTests)

#include "codex_app_server_client_tests.moc"
