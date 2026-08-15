#include "application/ai/CodexAgentTurnRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QtTest/QTest>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] ztermy::ai::CodexAppServerConfiguration configuration(const QStringList &arguments = {})
{
    return {.program =
                QCoreApplication::applicationDirPath() + QStringLiteral("/ztermy_codex_app_server_test_server.exe"),
            .arguments = arguments,
            .workingDirectory = QDir::tempPath(),
            .model = "test-model",
            .clientVersion = "0.3.0",
            .developerInstructions = "Use only the owning ztermy terminal tools.",
            .tools = {{.name = "read_terminal_frame",
                       .description = "Read the bounded frame of the owning terminal.",
                       .parametersJson = R"({"type":"object","additionalProperties":false})"}},
            .dynamicToolsVerified = true};
}

[[nodiscard]] bool initialize(ztermy::ai::CodexAgentTurnRunner &runner, QString &failure)
{
    QEventLoop loop;
    auto started = runner.initialize(configuration(), [&loop, &failure](auto result) {
        if (!result.has_value())
        {
            failure = QString::fromStdString(result.error().message);
        }
        loop.quit();
    });
    if (!started.has_value())
    {
        failure = QString::fromStdString(started.error().message);
        return false;
    }
    QTimer::singleShot(5'000, &loop, &QEventLoop::quit);
    loop.exec();
    return failure.isEmpty() && runner.ready();
}

class CodexAgentTurnRunnerTests final : public QObject
{
    Q_OBJECT

private slots:
    void streamsAndCompletesImmediateTool();
    void resumesAnAsynchronousToolResult();
    void cancelsActiveTurn();
};

void CodexAgentTurnRunnerTests::streamsAndCompletesImmediateTool()
{
    ztermy::ai::CodexAgentTurnRunner runner;
    QString failure;
    QVERIFY2(initialize(runner, failure), qPrintable(failure));
    QEventLoop loop;
    bool sawText = false;
    bool sawCompletion = false;
    bool sawToolOutput = false;
    std::optional<ztermy::ai::AiTurnMetrics> metrics;
    auto started = runner.start(
        "Inspect the terminal",
        [&sawText, &sawCompletion](const auto, const ztermy::ai::AiStreamEvent &event) {
            sawText = sawText || event.type == ztermy::ai::AiStreamEventType::textDelta;
            sawCompletion = sawCompletion || event.type == ztermy::ai::AiStreamEventType::responseCompleted;
        },
        [&loop, &metrics](const auto, const ztermy::ai::AiTurnMetrics &value) {
            metrics = value;
            loop.quit();
        },
        [](const ztermy::ai::AiToolCall &call)
            -> std::expected<ztermy::ai::AiTurnRunner::ToolHandlingResult, ztermy::ai::AiProviderError> {
            return ztermy::ai::AiTurnRunner::ToolHandlingResult{
                .output =
                    ztermy::ai::AiToolOutput{.callId = call.id, .name = call.name, .outputJson = R"({"ok":true})"}};
        },
        [&sawToolOutput](const auto &, const auto &) {
            sawToolOutput = true;
        });
    QVERIFY(started.has_value());
    QTimer::singleShot(5'000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(sawText);
    QVERIFY(sawCompletion);
    QVERIFY(sawToolOutput);
    QVERIFY(metrics.has_value());
    QVERIFY(metrics.value_or(ztermy::ai::AiTurnMetrics{}).firstTokenMilliseconds.has_value());
    QVERIFY(!runner.active());
    QVERIFY(runner.ready());
}

void CodexAgentTurnRunnerTests::resumesAnAsynchronousToolResult()
{
    ztermy::ai::CodexAgentTurnRunner runner;
    QString failure;
    QVERIFY2(initialize(runner, failure), qPrintable(failure));
    QEventLoop loop;
    bool pendingObserved = false;
    auto started = runner.start(
        "Inspect asynchronously", {},
        [&loop](const auto, const auto &) {
            loop.quit();
        },
        [&runner, &pendingObserved](const ztermy::ai::AiToolCall &call)
            -> std::expected<ztermy::ai::AiTurnRunner::ToolHandlingResult, ztermy::ai::AiProviderError> {
            const auto callGuard = std::make_shared<const ztermy::ai::AiToolCall>(call);
            QTimer::singleShot(0, &runner, [&runner, &pendingObserved, callGuard] noexcept {
                try
                {
                    pendingObserved = runner.pendingToolCall().has_value();
                    static_cast<void>(runner.completePendingTool(
                        ztermy::ai::AiToolOutput{.callId = callGuard->id,
                                                 .name = callGuard->name,
                                                 .outputJson = R"({"ok":true,"async":true})"}));
                }
                catch (...)
                {
                    pendingObserved = false;
                }
            });
            return ztermy::ai::AiTurnRunner::ToolHandlingResult{};
        });
    QVERIFY(started.has_value());
    QTimer::singleShot(5'000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(pendingObserved);
    QVERIFY(!runner.active());
}

void CodexAgentTurnRunnerTests::cancelsActiveTurn()
{
    ztermy::ai::CodexAgentTurnRunner runner;
    QString failure;
    QEventLoop readyLoop;
    auto initialized =
        runner.initialize(configuration({QStringLiteral("--no-tool")}), [&readyLoop, &failure](auto result) {
            if (!result.has_value())
            {
                failure = QString::fromStdString(result.error().message);
            }
            readyLoop.quit();
        });
    QVERIFY(initialized.has_value());
    QTimer::singleShot(5'000, &readyLoop, &QEventLoop::quit);
    readyLoop.exec();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    QEventLoop turnLoop;
    bool cancelled = false;
    auto started = runner.start(
        "Wait",
        [&cancelled](const auto, const ztermy::ai::AiStreamEvent &event) {
            cancelled = cancelled
                        || (event.type == ztermy::ai::AiStreamEventType::responseFailed && event.error.has_value()
                            && event.error->code == ztermy::ai::AiProviderErrorCode::cancelled);
        },
        [&turnLoop](const auto, const auto &) {
            turnLoop.quit();
        });
    QVERIFY(started.has_value());
    QVERIFY(runner.cancel());
    QTimer::singleShot(5'000, &turnLoop, &QEventLoop::quit);
    turnLoop.exec();
    QVERIFY(cancelled);
    QVERIFY(!runner.active());
}

} // namespace

QTEST_GUILESS_MAIN(CodexAgentTurnRunnerTests)

#include "codex_agent_turn_runner_tests.moc"
